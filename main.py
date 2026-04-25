# main.py

from fastapi import FastAPI, HTTPException
from pydantic import BaseModel
from typing import Optional
import psycopg2
import psycopg2.extras
import json
import uuid
import httpx
import asyncio
from datetime import datetime

app = FastAPI()

# --------------------
# CONNEXION POSTGRESQL

DB_CONFIG = {
    "dbname": "boss_ai",
    "user":"postgres",
    "password":"password",
    "host": "localhost",
    "port": 5432
}

def get_connection():
    return psycopg2.connect(**DB_CONFIG)

# -----------------
# MODELES PYDANTIC

class SessionCreate(BaseModel):
    session_id: str
    player_id:Optional[str] = "player_01"

class CombatCreate(BaseModel):
    combat_id:str
    session_id:str
    boss_hp_start: float = 100.0
    player_hp_start: float = 100.0

class CombatEnd(BaseModel):
    combat_id: str
    outcome:str
    boss_hp_end: float
    player_hp_end:float
    phase_max: int
    duration_sec:float

class EventCreate(BaseModel):
    session_id:str
    combat_id: str
    event_type: str   #dodge / parry / attack / hit / death
    timestamp: float
    value: float = 0.0
    context: str = ""

class AdaptationRequest(BaseModel):
    session_id: str
    combat_id:str

# --------------------------
# ROUTES SESSIONS ET COMBATS

@app.post("/sessions")
def create_session(data: SessionCreate):
    conn = get_connection()
    cur  = conn.cursor()
    try:
        cur.execute(
            "INSERT INTO sessions (id, player_id) VALUES (%s, %s)",
            (data.session_id, data.player_id)
        )
        conn.commit()
        return {"status": "ok", "session_id": data.session_id}
    except Exception as e:
        conn.rollback()
        raise HTTPException(status_code=500, detail=str(e))
    finally:
        cur.close()
        conn.close()

@app.post("/combats")
def create_combat(data: CombatCreate):
    conn = get_connection()
    cur  = conn.cursor()
    try:
        cur.execute("""
            INSERT INTO combats
                (id, session_id, boss_hp_start, player_hp_start)
            VALUES (%s, %s, %s, %s)
        """, (
            data.combat_id,
            data.session_id,
            data.boss_hp_start,
            data.player_hp_start
        ))
        conn.commit()
        return {"status": "ok", "combat_id": data.combat_id}
    except Exception as e:
        conn.rollback()
        raise HTTPException(status_code=500, detail=str(e))
    finally:
        cur.close()
        conn.close()

@app.post("/combats/end")
def end_combat(data: CombatEnd):
    conn = get_connection()
    cur = conn.cursor()
    try:
        cur.execute("""
            UPDATE combats SET
                ended_at = NOW(),
                outcome = %s,
                boss_hp_end = %s,
                player_hp_end = %s,
                phase_max = %s,
                duration_sec = %s
            WHERE id = %s
        """, (
            data.outcome,
            data.boss_hp_end,
            data.player_hp_end,
            data.phase_max,
            data.duration_sec,
            data.combat_id
        ))
        conn.commit()
        return {"status": "ok"}
    except Exception as e:
        conn.rollback()
        raise HTTPException(status_code=500, detail=str(e))
    finally:
        cur.close()
        conn.close()

# -------------------------------------------------------
# ROUTE EVENEMENTS
# -------------------------------------------------------

@app.post("/events")
def log_event(data: EventCreate):
    conn = get_connection()
    cur = conn.cursor()
    try:
        cur.execute("""
            INSERT INTO events
                (combat_id, event_type, timestamp, value, context)
            VALUES (%s, %s, %s, %s, %s)
        """, (
            data.combat_id,
            data.event_type,
            data.timestamp,
            data.value,
            data.context
        ))
        conn.commit()
        return {"status": "ok"}
    except Exception as e:
        conn.rollback()
        raise HTTPException(status_code=500, detail=str(e))
    finally:
        cur.close()
        conn.close()

# ------------------------
# CONSTRUCTION DU SNAPSHOT

def build_snapshot(session_id: str, combat_id: str) -> dict:
    conn = get_connection()
    cur = conn.cursor(cursor_factory=psycopg2.extras.RealDictCursor)

    try:
        # On recupere les 5 derniers combats de la session
        cur.execute("""
            SELECT id FROM combats
            WHERE session_id = %s
            ORDER BY started_at DESC
            LIMIT 5
        """, (session_id,))
        recent_combats = [row["id"] for row in cur.fetchall()]

        if not recent_combats:
            return {}

        combat_ids_placeholder = ",".join(["%s"] * len(recent_combats))

        # Taux d'esquive
        cur.execute(f"""
            SELECT
                COUNT(*) FILTER (WHERE event_type = 'dodge') AS dodge_count,
                COUNT(*) FILTER (WHERE event_type = 'attack') AS attack_count
            FROM events
            WHERE combat_id IN ({combat_ids_placeholder})
        """, recent_combats)
        row = cur.fetchone()
        attack_count = row["attack_count"] or 1
        dodge_rate   = round(row["dodge_count"] / attack_count, 2)

        # Taux de parade
        cur.execute(f"""
            SELECT COUNT(*) AS parry_count
            FROM events
            WHERE combat_id IN ({combat_ids_placeholder})
            AND event_type = 'parry'
        """, recent_combats)
        parry_count = cur.fetchone()["parry_count"] or 0
        parry_rate  = round(parry_count / attack_count, 2)

        # Distance de combat preferentielle (mediane via valeur)
        cur.execute(f"""
            SELECT PERCENTILE_CONT(0.5)
                WITHIN GROUP (ORDER BY value) AS preferred_range
            FROM events
            WHERE combat_id IN ({combat_ids_placeholder})
            AND event_type = 'position'
        """, recent_combats)
        preferred_range = cur.fetchone()["preferred_range"] or 200.0

        # Temps de reaction moyen apres une attaque du boss
        cur.execute(f"""
            SELECT AVG(value) AS avg_reaction_time
            FROM events
            WHERE combat_id IN ({combat_ids_placeholder})
            AND event_type = 'reaction'
        """, recent_combats)
        avg_reaction_time = cur.fetchone()["avg_reaction_time"] or 0.5

        # Vulnerabilite par type d'attaque
        cur.execute(f"""
            SELECT context AS attack_type,
                   COUNT(*) FILTER (WHERE event_type = 'hit') AS hits,
                   COUNT(*) AS total
            FROM events
            WHERE combat_id IN ({combat_ids_placeholder})
            AND event_type IN ('hit', 'dodge')
            GROUP BY context
        """, recent_combats)

        vulnerability_map = {}
        for row in cur.fetchall():
            if row["total"] > 0:
                vulnerability_map[row["attack_type"]] = round(
                    row["hits"] / row["total"], 2
                )

        # Nombre de tentatives dans la session
        cur.execute("""
            SELECT COUNT(*) AS attempt_count
            FROM combats
            WHERE session_id = %s
        """, (session_id,))
        attempt_count = cur.fetchone()["attempt_count"]

        return {
            "session_id":session_id,
            "combat_id":combat_id,
            "attempt_count":attempt_count,
            "dodge_rate":dodge_rate,
            "parry_rate":parry_rate,
            "preferred_range":round(float(preferred_range), 1),
            "avg_reaction_time":round(float(avg_reaction_time), 3),
            "attack_vulnerability":vulnerability_map
        }

    finally:
        cur.close()
        conn.close()

# ----------
# COUCHE LLM

OPENAI_API_KEY = "sk-..."  # Je cache ma cle pour des soucis de securite

SYSTEM_PROMPT = """
Tu es un moteur de strategie pour un boss de jeu video de type Souls-like.
Tu recois des statistiques de combat decrivant le comportement du joueur.
Tu dois retourner UNIQUEMENT un objet JSON valide, sans aucun texte en dehors.

Le JSON doit respecter exactement ce schema :
{
    "attack_weights": {
        "heavy_attack": 0.0,
        "light_attack":  0.0,
        "area_attack":   0.0,
        "feint":         0.0
    },
    "aggression_level": 0.0,
    "reasoning": "..."
}

Regles obligatoires :
- La somme de attack_weights doit etre egale a 1.0
- aggression_level doit etre compris entre 0.0 et 1.0
- reasoning doit expliquer brievement pourquoi ces valeurs ont ete choisies
"""

async def call_llm(snapshot: dict) -> dict:
    user_prompt = f"""
Voici les statistiques de combat du joueur sur les derniers affrontements :

- Nombre de tentatives : {snapshot.get('attempt_count', 0)}
- Taux d'esquive : {snapshot.get('dodge_rate', 0)}
  (proportion des attaques du boss evitees par esquive)
- Taux de parade : {snapshot.get('parry_rate', 0)}
  (proportion des attaques du boss contrees)
- Distance de combat preferee : {snapshot.get('preferred_range', 200)} unites
- Temps de reaction moyen apres une attaque : {snapshot.get('avg_reaction_time', 0.5)} secondes
- Vulnerabilite par type d'attaque : {json.dumps(snapshot.get('attack_vulnerability', {}), ensure_ascii=False)}

Attaques disponibles pour le boss :
- heavy_attack : attaque lente et puissante, difficile a esquiver
- light_attack : attaque rapide, fenetre d'esquive courte
- area_attack : attaque de zone, non esquivable par dodge lateral
- feint : fausse attaque pour tromper le joueur

Retourne uniquement le JSON d'adaptation.
"""

    async with httpx.AsyncClient() as client:
        response = await client.post(
            "https://api.openai.com/v1/chat/completions",
            headers={
                "Authorization": f"Bearer {OPENAI_API_KEY}",
                "Content-Type":  "application/json"
            },
            json={
                "model": "gpt-4o",
                "max_tokens": 500,
                "temperature": 0.3,
                "messages": [
                    {"role": "system", "content": SYSTEM_PROMPT},
                    {"role": "user",   "content": user_prompt}
                ]
            },
            timeout=30.0
        )

    if response.status_code != 200:
        raise Exception(f"OpenAI a retourne le code {response.status_code}")

    content = response.json()["choices"][0]["message"]["content"]
    return json.loads(content)

# ----------------------------
# VALIDATION DE LA REPONSE LLM

def validate_adaptation(raw: dict) -> dict:
    validated = {}

    # Validation de aggression_level
    aggression = raw.get("aggression_level", 0.5)
    validated["aggression_level"] = max(0.0, min(1.0, float(aggression)))

    # Validation de attack_weights
    weights = raw.get("attack_weights", {})
    valid_keys = {"heavy_attack", "light_attack", "area_attack", "feint"}
    cleaned_weights = {}

    for key in valid_keys:
        value = weights.get(key, 0.0)
        cleaned_weights[key] = max(0.0, min(1.0, float(value)))

    # Normalisation
    total = sum(cleaned_weights.values())
    if total > 0:
        for key in cleaned_weights:
            cleaned_weights[key] = round(cleaned_weights[key] / total, 3)
    else:
        # Valeurs par defaut si tout est a zero
        cleaned_weights = {
            "heavy_attack": 0.3,
            "light_attack":0.4,
            "area_attack":0.2,
            "feint":0.1
        }

    validated["attack_weights"]= cleaned_weights
    validated["reasoning"]= raw.get("reasoning", "")

    return validated

# ----------------
# ROUTE ADAPTATION

@app.post("/adaptation")
async def request_adaptation(data: AdaptationRequest):
    # Construction du snapshot
    snapshot = build_snapshot(data.session_id, data.combat_id)

    if not snapshot:
        raise HTTPException(
            status_code=404,
            detail="Aucune donnee de combat trouvee pour cette session"
        )

    # Appel au LLM
    try:
        raw_response = await call_llm(snapshot)
    except Exception as e:
        raise HTTPException(
            status_code=503,
            detail=f"Erreur lors de l'appel au LLM : {str(e)}"
        )

    # Validation de la reponse
    validated = validate_adaptation(raw_response)

    # Archivage en base
    conn = get_connection()
    cur  = conn.cursor()
    try:
        cur.execute("""
            INSERT INTO snapshots (session_id, snapshot_data, llm_response)
            VALUES (%s, %s, %s)
        """, (
            data.session_id,
            json.dumps(snapshot),
            json.dumps(validated)
        ))
        conn.commit()
    finally:
        cur.close()
        conn.close()

    return validated