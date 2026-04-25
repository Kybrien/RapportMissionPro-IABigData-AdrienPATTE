#include "./LLMBridgeComponent.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Json.h"
#include "JsonUtilities.h"

ULLMBridgeComponent::ULLMBridgeComponent()
{
    PrimaryComponentTick.bCanEverTick = false;

    // Valeurs par defaut des attack_weights
    LastValidAdaptation.AttackWeights.Add(TEXT("heavy_attack"),0.3f);
    LastValidAdaptation.AttackWeights.Add(TEXT("light_attack"),0.4f);
    LastValidAdaptation.AttackWeights.Add(TEXT("area_attack"),0.2f);
    LastValidAdaptation.AttackWeights.Add(TEXT("feint"),0.1f);
    LastValidAdaptation.AggressionLevel = 0.5f;
}

void ULLMBridgeComponent::BeginPlay()
{
    Super::BeginPlay();
}

void ULLMBridgeComponent::StartSession(
    const FString& NewSessionId,
    const FString& NewCombatId)
{
    SessionId = NewSessionId;
    CombatId  = NewCombatId;

    UE_LOG(LogTemp, Log,
        TEXT("LLMBridge : nouvelle session %s / combat %s"),
        *SessionId, *CombatId);
}

// -------------------------------------------------------
// LOG COMBAT EVENT

void ULLMBridgeComponent::LogCombatEvent(const FCombatEvent& Event)
{
    // Construction du JSON a envoyer
    TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject());
    JsonObject->SetStringField(TEXT("session_id"),SessionId);
    JsonObject->SetStringField(TEXT("combat_id"),CombatId);
    JsonObject->SetStringField(TEXT("event_type"), Event.EventType);
    JsonObject->SetNumberField(TEXT("timestamp"),Event.Timestamp);
    JsonObject->SetNumberField(TEXT("value"), Event.Value);
    JsonObject->SetStringField(TEXT("context"),Event.Context);

    FString JsonBody;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonBody);
    FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);

    // Creation de la requete HTTP asynchrone
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();

    Request->SetURL(BackendURL + TEXT("/events"));
    Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    Request->SetContentAsString(JsonBody);

    // Callback de logging en cas d'erreur uniquement
    Request->OnProcessRequestComplete().BindUObject(this,&ULLMBridgeComponent::OnLogEventResponse);

    // Fire and forget : on envoie sans bloquer le thread de jeu
    Request->ProcessRequest();
}

void ULLMBridgeComponent::OnLogEventResponse(FHttpRequestPtr Request,FHttpResponsePtr Response,bool bSuccess)
{
    if (!bSuccess || !Response.IsValid())
    {
        UE_LOG(LogTemp, Warning,
            TEXT("LLMBridge : echec envoi evenement de combat"));
        return;
    }

    if (Response->GetResponseCode() != 200)
    {
        UE_LOG(LogTemp, Warning,TEXT("LLMBridge : backend a retourne le code %d"),Response->GetResponseCode());
    }
}

// -------------------------------------------------------
// REQUEST ADAPTATION

void ULLMBridgeComponent::RequestAdaptation()
{
    // Construction du JSON de la requete snapshot
    TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject());
    JsonObject->SetStringField(TEXT("session_id"),SessionId);
    JsonObject->SetStringField(TEXT("combat_id"),CombatId);

    FString JsonBody;
    TSharedRef<TJsonWriter<>> Writer =
        TJsonWriterFactory<>::Create(&JsonBody);
    FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request =
        FHttpModule::Get().CreateRequest();

    Request->SetURL(BackendURL + TEXT("/adaptation"));
    Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    Request->SetContentAsString(JsonBody);

    Request->OnProcessRequestComplete().BindUObject(
        this,
        &ULLMBridgeComponent::OnAdaptationResponse
    );

    Request->ProcessRequest();

    UE_LOG(LogTemp, Log,
        TEXT("LLMBridge : demande d'adaptation envoyee pour le combat %s"),
        *CombatId);
}

void ULLMBridgeComponent::OnAdaptationResponse(
    FHttpRequestPtr Request,
    FHttpResponsePtr Response,
    bool bSuccess)
{
    // Si la requete echoue, on conserve les derniers parametres valides
    if (!bSuccess || !Response.IsValid())
    {
        UE_LOG(LogTemp, Warning,
            TEXT("LLMBridge : echec reception adaptation, "
                 "conservation des parametres precedents"));
        return;
    }

    if (Response->GetResponseCode() != 200)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("LLMBridge : code HTTP %d, parametres inchanges"),
            Response->GetResponseCode());
        return;
    }

    FBossAdaptation NewAdaptation;
    bool bValid = ParseAndValidateAdaptation(
        Response->GetContentAsString(),
        NewAdaptation
    );

    if (!bValid)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("LLMBridge : reponse invalide, parametres inchanges"));
        return;
    }

    // Mise a jour des parametres valides et notification Blueprint
    LastValidAdaptation = NewAdaptation;
    OnAdaptationReceived.Broadcast(LastValidAdaptation);

    UE_LOG(LogTemp, Log,
        TEXT("LLMBridge : adaptation appliquee | aggression=%.2f | reasoning=%s"),
        LastValidAdaptation.AggressionLevel,
        *LastValidAdaptation.Reasoning);
}

// -------------------------------------------------------
// PARSING ET VALIDATION

bool ULLMBridgeComponent::ParseAndValidateAdaptation(
    const FString& JsonString,
    FBossAdaptation& OutAdaptation)
{
    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader =
        TJsonReaderFactory<>::Create(JsonString);

    if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
    {
        UE_LOG(LogTemp, Warning,
            TEXT("LLMBridge : JSON invalide -> %s"), *JsonString);
        return false;
    }

    // Validation de aggression_level
    double AggressionLevel = 0.5;
    if (JsonObject->TryGetNumberField(TEXT("aggression_level"), AggressionLevel))
    {
        // Clamp entre 0.0 et 1.0 si hors bornes
        OutAdaptation.AggressionLevel =
            FMath::Clamp((float)AggressionLevel, 0.0f, 1.0f);
    }
    else
    {
        UE_LOG(LogTemp, Warning,
            TEXT("LLMBridge : champ aggression_level manquant, valeur par defaut"));
        OutAdaptation.AggressionLevel = LastValidAdaptation.AggressionLevel;
    }

    // Validation de attack_weights
    const TSharedPtr<FJsonObject>* WeightsObject;
    if (JsonObject->TryGetObjectField(TEXT("attack_weights"), WeightsObject))
    {
        for (auto& Pair : (*WeightsObject)->Values)
        {
            double Weight = 0.0;
            if (Pair.Value->TryGetNumber(Weight))
            {
                OutAdaptation.AttackWeights.Add(
                    Pair.Key,
                    FMath::Clamp((float)Weight, 0.0f, 1.0f)
                );
            }
        }

        // Normalisation pour que la somme soit egale a 1.0
        NormalizeAttackWeights(OutAdaptation.AttackWeights);
    }
    else
    {
        UE_LOG(LogTemp, Warning,
            TEXT("LLMBridge : attack_weights manquant, valeurs par defaut"));
        OutAdaptation.AttackWeights = LastValidAdaptation.AttackWeights;
    }

    // Recuperation du reasoning (optionnel)
    FString Reasoning;
    if (JsonObject->TryGetStringField(TEXT("reasoning"), Reasoning))
    {
        OutAdaptation.Reasoning = Reasoning;
    }

    return true;
}

void ULLMBridgeComponent::NormalizeAttackWeights(TMap<FString, float>& Weights)
{
    if (Weights.Num() == 0) return;

    float Total = 0.0f;
    for (auto& Pair : Weights)
    {
        Total += Pair.Value;
    }

    // Si la somme est deja correcte, rien a faire
    if (FMath::IsNearlyEqual(Total, 1.0f, 0.001f)) return;

    // Sinon on normalise chaque valeur
    if (Total > 0.0f)
    {
        for (auto& Pair : Weights)
        {
            Pair.Value /= Total;
        }
    }
}