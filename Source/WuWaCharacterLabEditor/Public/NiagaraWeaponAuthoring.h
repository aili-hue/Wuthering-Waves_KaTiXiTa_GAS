#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "NiagaraWeaponAuthoring.generated.h"

class UNiagaraSystem;

/** Editor-only access to native Niagara graph authoring missing from Python. */
UCLASS()
class WUWACHARACTERLABEDITOR_API UNiagaraWeaponAuthoring : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category="WuWa|Niagara")
    static FString DescribeModuleScript(const FString& ScriptPath);

    /** Creates a new system only; the caller saves it after compilation and checks. */
    UFUNCTION(BlueprintCallable, Category="WuWa|Niagara")
    static UNiagaraSystem* CreateWeaponSystem(const FString& PackagePath, const FString& SpecificationJSON);

    /** Edits existing, unlinked input defaults after checking every expected value; does not save. */
    UFUNCTION(BlueprintCallable, Category="WuWa|Niagara")
    static FString SetWeaponGraphDefaults(UNiagaraSystem* System, const FString& ChangesJSON);

    /** Reassigns one checked native module and inserts its native output assignments; does not save. */
    UFUNCTION(BlueprintCallable, Category="WuWa|Niagara")
    static FString ReplaceWeaponModule(UNiagaraSystem* System, const FString& EmitterName,
        const FString& UsageName, const FString& ModuleName, const FString& ExpectedScriptPath,
        const FString& ReplacementJSON);

    /** Swaps adjacent native modules while preserving their graph branches; does not save. */
    UFUNCTION(BlueprintCallable, Category="WuWa|Niagara")
    static FString MoveWeaponModuleBefore(UNiagaraSystem* System, const FString& EmitterName,
        const FString& UsageName, const FString& ModuleName, const FString& BeforeModuleName);

    UFUNCTION(BlueprintCallable, Category="WuWa|Niagara")
    static FString AuditWeaponSystem(UNiagaraSystem* System, bool bCompile = false, bool bIncludeTranslation = false);

    /** Copies one compiled script for offline verification; does not compile or modify the asset. */
    UFUNCTION(BlueprintCallable, Category="WuWa|Niagara")
    static FString DescribeWeaponScriptBytecode(UNiagaraSystem* System, const FString& EmitterName,
        const FString& UsageName);
};
