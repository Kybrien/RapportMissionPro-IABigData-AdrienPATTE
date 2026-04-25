# IA Adaptative LLM · Boss Souls-like · Unreal Engine 5

Code source du rapport de mission professionnelle **ME Expert IT · Applications intelligentes et Big Data**

**Auteur :** Adrien PATTE · Candidat 163306
**Entreprise :** Born To Web (Paris)
**Sujet :** Conception d'une intelligence artificielle adaptative basée sur LLM pour un boss dynamique dans un jeu Souls-like sous Unreal Engine

---

## Présentation du projet

Ce dépôt contient le code source de l'architecture hybride développée pendant le stage. Le système permet à un boss de jeu vidéo d'adapter son comportement en fonction du profil du joueur, en combinant :

- Un **Behavior Tree Unreal Engine 5** pour l'exécution temps réel du combat
- Un **backend Python FastAPI** pour la collecte des données et la communication avec le LLM
- **GPT-4o (OpenAI)** comme couche de décision stratégique asynchrone
- **PostgreSQL** pour la persistance des événements de combat et des snapshots

Le LLM n'intervient jamais en temps réel. Il analyse périodiquement les données des derniers combats et propose des ajustements de comportement (probabilités d'attaque, niveau d'agressivité) que le Behavior Tree applique lors des phases suivantes.

---

## Structure du dépôt

```
LLMBridgeComponent.h      # Header du composant C++ Unreal Engine
LLMBridgeComponent.cpp    # Implémentation · communication HTTP asynchrone avec le backend
main.py                   # Service FastAPI · routes, snapshot builder, appel LLM, validation
schema.sql                # Schéma PostgreSQL · sessions, combats, events, snapshots
requirements.txt          # Dépendances Python
README.md
```

---

## Architecture en 3 couches

```
Unreal Engine 5          Python / FastAPI          OpenAI API
BehaviorTree        -->  /events  (POST)       -->  GPT-4o
LLMBridgeComponent  -->  /adaptation (POST)   <--  (JSON réponse)
Blackboard          <--  validate + clamp
```

Tous les appels sont **asynchrones** : la boucle de jeu n'est jamais bloquée.

---

## Prérequis

### Python

```
Python 3.11+
fastapi
uvicorn
httpx
psycopg2-binary
pydantic
```

Installation :

```bash
pip install -r requirements.txt
```

### PostgreSQL

```
PostgreSQL 16
Port : 5432
Base : boss_ai
```

Initialisation du schéma :

```bash
psql -U postgres -f schema.sql
```

### Unreal Engine

```
Unreal Engine 5 (Blueprints & C++)
Modules requis : Http, Json, JsonUtilities
```

Ajouter dans `HD2D_Template.Build.cs` :

```csharp
PublicDependencyModuleNames.AddRange(new string[] {
    "Core", "CoreUObject", "Engine", "InputCore",
    "Http", "Json", "JsonUtilities"
});
```

---

## Lancement du backend

```bash
python -m uvicorn main:app --reload
```

Le serveur démarre sur `http://127.0.0.1:8000`.
Documentation Swagger disponible sur `http://127.0.0.1:8000/docs`.

---

## Configuration

Dans `main.py`, remplacer la clé API :

```python
OPENAI_API_KEY = "sk-..."  # Votre clé OpenAI
```

Dans Unreal Engine, le composant `LLMBridgeComponent` expose les propriétés configurables suivantes dans l'éditeur :

- `BackendURL` : URL du service Python (défaut : `http://127.0.0.1:8000`)
- `SessionId` : identifiant de la session courante
- `CombatId` : identifiant du combat en cours

---

## Format de la réponse LLM

Le LLM retourne un objet JSON structuré :

```json
{
  "attack_weights": {
    "heavy_attack": 0.15,
    "light_attack": 0.20,
    "area_attack": 0.50,
    "feint": 0.15
  },
  "aggression_level": 0.72,
  "reasoning": "Le joueur esquive très fréquemment (82%)..."
}
```

La somme des `attack_weights` est toujours égale à 1.0 (normalisée côté Python si nécessaire).
`aggression_level` est clampé entre 0.0 et 1.0.

---

## Contexte académique

Ce code a été développé dans le cadre du stage de première année du **Mastère Européen Expert IT · Applications intelligentes et Big Data** (FEDE / Gaming Campus Paris), au sein de la société **Born To Web**, sous la direction de M. Julien Lichtle.

Le rapport complet de mission professionnelle (UC D42) documente l'architecture, les choix techniques, les résultats et les limites du système.
