#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Http.h"
#include "LLMBridgeComponent.generated.h"

// Structure representant un evenement de combat envoye a PostgreSQL
USTRUCT(BlueprintType)
struct FCombatEvent
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadWrite, Category = "LLM Bridge")FString EventType;// "dodge", "parry", "attack", "hit", "death"
    UPROPERTY(BlueprintReadWrite, Category = "LLM Bridge")float Timestamp;// Temps ecoule depuis le debut du combat
    UPROPERTY(BlueprintReadWrite, Category = "LLM Bridge")float Value;// Valeur numerique contextuelle
    UPROPERTY(BlueprintReadWrite, Category = "LLM Bridge")FString Context; // Contexte supplementaire
};

// Structure representant les parametres d'adaptation retournes par le LLM
USTRUCT(BlueprintType)
struct FBossAdaptation
{
    GENERATED_BODY()
    // Probabilites pour chaque type d'attaque (somme = 1.0)
    UPROPERTY(BlueprintReadWrite, Category = "LLM Bridge")TMap<FString, float> AttackWeights;
    // Niveau d'agressivite du boss entre 0.0 et 1.0
    UPROPERTY(BlueprintReadWrite, Category = "LLM Bridge")float AggressionLevel;
    // Justification textuelle du LLM (pour le debug)
    UPROPERTY(BlueprintReadWrite, Category = "LLM Bridge")FString Reasoning;
    // Constructeur avec valeurs par defaut
    FBossAdaptation(){AggressionLevel = 0.5f;}
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FOnAdaptationReceived,
    const FBossAdaptation&, Adaptation
);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class HD2D_TEMPLATE_API ULLMBridgeComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    ULLMBridgeComponent();
protected:
    virtual void BeginPlay() override;
public:
    // URL du service Python FastAPI
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LLM Bridge | Config")FString BackendURL = TEXT("http://127.0.0.1:8000");
    // Identifiant de la session courante
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LLM Bridge | Config")FString SessionId;
    // Identifiant du combat en cours
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LLM Bridge | Config")FString CombatId;
    // Derniers parametres valides recus du LLM
    UPROPERTY(BlueprintReadOnly, Category = "LLM Bridge | State")FBossAdaptation LastValidAdaptation;
    // Declenche quand une adaptation est recue et validee
    UPROPERTY(BlueprintAssignable, Category = "LLM Bridge | Events")FOnAdaptationReceived OnAdaptationReceived;
    // Envoie un evenement de combat au backend (fire and forget)
    UFUNCTION(BlueprintCallable, Category = "LLM Bridge")void LogCombatEvent(const FCombatEvent& Event);
    // Demande une adaptation au LLM via le backend
    UFUNCTION(BlueprintCallable, Category = "LLM Bridge")void RequestAdaptation();
    // Initialise une nouvelle session
    UFUNCTION(BlueprintCallable, Category = "LLM Bridge")
    void StartSession(const FString& NewSessionId, const FString& NewCombatId);
private:
    // Callback appele quand le backend repond a LogCombatEvent
    void OnLogEventResponse(FHttpRequestPtr Request,FHttpResponsePtr Response,bool bSuccess);
    // Callback appele quand le backend repond a RequestAdaptation
    void OnAdaptationResponse(FHttpRequestPtr Request,FHttpResponsePtr Response,bool bSuccess);
    // Valide et parse la reponse JSON du LLM
    bool ParseAndValidateAdaptation(const FString& JsonString,FBossAdaptation& OutAdaptation);
    // Normalise les attack_weights pour que leur somme soit egale a 1.0
    void NormalizeAttackWeights(TMap<FString, float>& Weights);
};