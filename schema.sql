-- Creation de la base
CREATE DATABASE boss_ai;

-- Table sessions
CREATE TABLE sessions (
                          id          VARCHAR(64) PRIMARY KEY,
                          created_at  TIMESTAMP DEFAULT NOW(),
                          player_id   VARCHAR(64)
);

-- Table combats
CREATE TABLE combats (
                         id              VARCHAR(64) PRIMARY KEY,
                         session_id      VARCHAR(64) REFERENCES sessions(id),
                         started_at      TIMESTAMP DEFAULT NOW(),
                         ended_at        TIMESTAMP,
                         outcome         VARCHAR(16),  -- 'victory' ou 'defeat'
                         phase_max       INTEGER DEFAULT 1,
                         duration_sec    FLOAT,
                         boss_hp_start   FLOAT,
                         boss_hp_end     FLOAT,
                         player_hp_start FLOAT,
                         player_hp_end   FLOAT
);

-- Table events
CREATE TABLE events (
                        id          SERIAL PRIMARY KEY,
                        combat_id   VARCHAR(64) REFERENCES combats(id),
                        event_type  VARCHAR(32),  -- 'dodge', 'parry', 'attack', 'hit', 'death'
                        timestamp   FLOAT,        -- secondes depuis le debut du combat
                        value       FLOAT,        -- degats, distance, ou autre valeur contextuelle
                        context     TEXT          -- informations supplementaires en texte libre
);

-- Table snapshots
CREATE TABLE snapshots (
                           id              SERIAL PRIMARY KEY,
                           session_id      VARCHAR(64) REFERENCES sessions(id),
                           created_at      TIMESTAMP DEFAULT NOW(),
                           snapshot_data   JSONB,    -- resume statistique envoye au LLM
                           llm_response    JSONB     -- reponse brute du LLM
);