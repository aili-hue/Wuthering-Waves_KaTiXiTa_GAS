#include "NiagaraWeaponAuthoring.h"

#include "Dom/JsonObject.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_Niagara.h"
#include "Engine/StaticMesh.h"
#include "Curves/CurveFloat.h"
#include "Curves/CurveLinearColor.h"
#include "Curves/CurveVector.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "Misc/Base64.h"
#include "Misc/PackageName.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterFactoryNew.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraDataInterfaceCurve.h"
#include "NiagaraDataInterfaceColorCurve.h"
#include "NiagaraDataInterfaceVectorCurve.h"
#include "NiagaraDataInterfaceVector2DCurve.h"
#include "NiagaraGraph.h"
#include "NiagaraMeshRendererProperties.h"
#include "NiagaraNodeAssignment.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeInput.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraParameterMapHistory.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemFactoryNew.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "UpgradeNiagaraScriptResults.h"
#include "ViewModels/Stack/NiagaraParameterHandle.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"

namespace
{
using FObject = TSharedPtr<FJsonObject>;

FString SerializeJSON(const FObject& Object)
{
    FString Text;
    FJsonSerializer::Serialize(Object.ToSharedRef(), TJsonWriterFactory<>::Create(&Text));
    return Text;
}

UEdGraphPin* ParameterMapPin(UNiagaraNode& Node, EEdGraphPinDirection Direction)
{
    for (UEdGraphPin* Pin : Node.Pins)
    {
        if (Pin->Direction == Direction
            && UEdGraphSchema_Niagara::PinToTypeDefinition(Pin) == FNiagaraTypeDefinition::GetParameterMapDef()) return Pin;
    }
    return nullptr;
}

void OrderedModules(UNiagaraNodeOutput& Output, TArray<UNiagaraNodeFunctionCall*>& Modules)
{
    UNiagaraNode* Node = &Output;
    TSet<UNiagaraNode*> Visited;
    while (Node && !Visited.Contains(Node))
    {
        Visited.Add(Node);
        UEdGraphPin* Input = ParameterMapPin(*Node, EGPD_Input);
        Node = Input && Input->LinkedTo.Num() == 1
            ? Cast<UNiagaraNode>(Input->LinkedTo[0]->GetOwningNode()) : nullptr;
        if (UNiagaraNodeFunctionCall* Module = Cast<UNiagaraNodeFunctionCall>(Node)) Modules.Insert(Module, 0);
    }
}

FNiagaraTypeDefinition InputType(const FString& Name)
{
    if (Name == TEXT("float")) return FNiagaraTypeDefinition::GetFloatDef();
    if (Name == TEXT("int")) return FNiagaraTypeDefinition::GetIntDef();
    if (Name == TEXT("bool")) return FNiagaraTypeDefinition::GetBoolDef();
    if (Name == TEXT("vec2")) return FNiagaraTypeDefinition::GetVec2Def();
    if (Name == TEXT("vec3")) return FNiagaraTypeDefinition::GetVec3Def();
    if (Name == TEXT("vec4")) return FNiagaraTypeDefinition::GetVec4Def();
    if (Name == TEXT("color")) return FNiagaraTypeDefinition::GetColorDef();
    if (Name == TEXT("quat")) return FNiagaraTypeDefinition::GetQuatDef();
    if (Name == TEXT("position")) return FNiagaraTypeDefinition::GetPositionDef();
    if (Name == TEXT("curve")) return FNiagaraTypeDefinition(UNiagaraDataInterfaceCurve::StaticClass());
    if (Name == TEXT("color_curve")) return FNiagaraTypeDefinition(UNiagaraDataInterfaceColorCurve::StaticClass());
    if (Name == TEXT("vector_curve")) return FNiagaraTypeDefinition(UNiagaraDataInterfaceVectorCurve::StaticClass());
    if (Name == TEXT("vector2_curve")) return FNiagaraTypeDefinition(UNiagaraDataInterfaceVector2DCurve::StaticClass());
    if (Name.StartsWith(TEXT("enum:")))
    {
        if (UEnum* Enum = LoadObject<UEnum>(nullptr, *Name.Mid(5))) return FNiagaraTypeDefinition(Enum);
    }
    return FNiagaraTypeDefinition();
}

bool ValidColorDefault(const FNiagaraTypeDefinition& Type, const FString& Text)
{
    if (Type != FNiagaraTypeDefinition::GetColorDef()) return true;
    FLinearColor Color = FLinearColor::Black;
    return Color.InitFromString(Text) && FMath::IsFinite(Color.R) && FMath::IsFinite(Color.G)
        && FMath::IsFinite(Color.B) && FMath::IsFinite(Color.A);
}

void DescribeColorDefault(const FObject& Result, const FNiagaraTypeDefinition& Type, const FString& Text)
{
    if (Type != FNiagaraTypeDefinition::GetColorDef()) return;
    FLinearColor Color = FLinearColor::Black;
    const bool bValid = ValidColorDefault(Type, Text) && Color.InitFromString(Text);
    Result->SetBoolField(TEXT("color_default_valid"), bValid);
    if (!bValid) return;
    TArray<TSharedPtr<FJsonValue>> Components;
    for (float Component : {Color.R, Color.G, Color.B, Color.A})
        Components.Add(MakeShared<FJsonValueNumber>(Component));
    Result->SetArrayField(TEXT("parsed_color"), Components);
}

bool ConfigureFunction(UNiagaraNodeFunctionCall& Function, const FObject& Settings,
    const FCompileConstantResolver& ConstantResolver, FString& Error)
{
    const UEdGraphSchema_Niagara* Schema = GetDefault<UEdGraphSchema_Niagara>();
    const FObject* Switches;
    if (Settings->TryGetObjectField(TEXT("switches"), Switches))
    {
        for (const auto& Pair : (*Switches)->Values)
        {
            UEdGraphPin* Pin = Function.FindPin(FName(*Pair.Key), EGPD_Input);
            if (!Pin)
            {
                Error = TEXT("Missing static switch: ") + Function.GetFunctionName() + TEXT(".") + Pair.Key;
                return false;
            }
            Schema->TrySetDefaultValue(*Pin, Pair.Value->AsString());
        }
    }
    const FObject* Inputs;
    if (!Settings->TryGetObjectField(TEXT("inputs"), Inputs)) return true;
    TArray<FNiagaraVariable> DeclaredInputs;
    TSet<FNiagaraVariable> HiddenInputs;
    FNiagaraStackGraphUtilities::GetStackFunctionInputs(Function, DeclaredInputs, HiddenInputs,
        ConstantResolver, FNiagaraStackGraphUtilities::ENiagaraGetStackFunctionInputPinsOptions::ModuleInputsOnly);
    for (const auto& Pair : (*Inputs)->Values)
    {
        const FString Key(*Pair.Key);
        const FObject Value = Pair.Value->AsObject();
        const FNiagaraTypeDefinition Type = InputType(Value->GetStringField(TEXT("type")));
        if (!Type.IsValid())
        {
            Error = TEXT("Unsupported Niagara input type: ") + Key;
            return false;
        }
        const FName ModuleName(*(TEXT("Module.") + Key));
        const FNiagaraVariable* Declared = DeclaredInputs.FindByPredicate(
            [ModuleName](const FNiagaraVariable& Variable) { return Variable.GetName() == ModuleName; });
        if (!Declared || Declared->GetType() != Type || HiddenInputs.Contains(*Declared))
        {
            Error = TEXT("Missing, hidden or differently typed native input: ")
                + Function.GetFunctionName() + TEXT(".") + Key;
            return false;
        }
        const FNiagaraParameterHandle Handle = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(
            FNiagaraParameterHandle(ModuleName), &Function);
        UEdGraphPin& Pin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(
            Function, Handle, Type, FGuid(), FGuid());
        FString Link;
        FString DynamicPath;
        FString CurvePath;
        if (Value->TryGetStringField(TEXT("link"), Link))
        {
            const FNiagaraVariableBase Variable(Type, FName(*Link));
            TSet<FNiagaraVariableBase> KnownParameters;
            KnownParameters.Add(Variable);
            FNiagaraStackGraphUtilities::SetLinkedParameterValueForFunctionInput(Pin, Variable, KnownParameters);
        }
        else if (Value->TryGetStringField(TEXT("dynamic"), DynamicPath))
        {
            UNiagaraScript* Script = LoadObject<UNiagaraScript>(nullptr, *DynamicPath);
            if (!Script)
            {
                Error = TEXT("Missing native dynamic input: ") + DynamicPath;
                return false;
            }
            UNiagaraNodeFunctionCall* DynamicFunction = nullptr;
            FNiagaraStackGraphUtilities::SetDynamicInputForFunctionInput(Pin, Script, DynamicFunction);
            if (!DynamicFunction || !ConfigureFunction(*DynamicFunction, Value, ConstantResolver, Error)) return false;
        }
        else if (Value->TryGetStringField(TEXT("curve_asset"), CurvePath))
        {
            UCurveBase* Curve = LoadObject<UCurveBase>(nullptr, *CurvePath);
            const bool bFloatCurve = Type.GetClass() == UNiagaraDataInterfaceCurve::StaticClass() && Cast<UCurveFloat>(Curve);
            const bool bColorCurve = Type.GetClass() == UNiagaraDataInterfaceColorCurve::StaticClass() && Cast<UCurveLinearColor>(Curve);
            const bool bVectorCurve = Type.GetClass() == UNiagaraDataInterfaceVectorCurve::StaticClass() && Cast<UCurveVector>(Curve);
            const bool bVector2Curve = Type.GetClass() == UNiagaraDataInterfaceVector2DCurve::StaticClass() && Cast<UCurveVector>(Curve);
            if (!bFloatCurve && !bColorCurve && !bVectorCurve && !bVector2Curve)
            {
                Error = TEXT("Missing or differently typed native curve asset: ") + CurvePath;
                return false;
            }
            UNiagaraDataInterface* Interface = nullptr;
            FNiagaraStackGraphUtilities::SetDataInterfaceValueForFunctionInput(
                Pin, Type.GetClass(), Key, Interface);
            UNiagaraDataInterfaceCurveBase* CurveInterface = Cast<UNiagaraDataInterfaceCurveBase>(Interface);
            if (!CurveInterface) { Error = TEXT("Could not create native curve input: ") + Key; return false; }
            CurveInterface->CurveAsset = Curve;
            CurveInterface->bUseLUT = false;
            CurveInterface->bOptimizeLUT = false;
            CurveInterface->SyncCurvesToAsset();
            // UE's Vector2D interface does not override SyncCurvesToAsset.
            if (UNiagaraDataInterfaceVector2DCurve* VectorCurve = Cast<UNiagaraDataInterfaceVector2DCurve>(CurveInterface))
            {
                const UCurveVector* Asset = CastChecked<UCurveVector>(Curve);
                VectorCurve->XCurve = Asset->FloatCurves[0];
                VectorCurve->YCurve = Asset->FloatCurves[1];
            }
            CurveInterface->UpdateLUT();
        }
        else
        {
            const FString Default = Value->GetStringField(TEXT("value"));
            if (!ValidColorDefault(Type, Default))
            {
                Error = TEXT("Invalid native linear color default: ") + Key;
                return false;
            }
            Schema->TrySetDefaultValue(Pin, Default);
        }
    }
    return true;
}

bool AddModules(UNiagaraGraph& Graph, const FObject& Settings,
    const UNiagaraSystem* System, const FVersionedNiagaraEmitter& Emitter, FString& Error,
    int32 InsertIndex = INDEX_NONE)
{
    const TArray<TSharedPtr<FJsonValue>>* Modules;
    if (!Settings->TryGetArrayField(TEXT("modules"), Modules)) return true;
    for (const auto& Item : *Modules)
    {
        const FObject Module = Item->AsObject();
        const int64 UsageValue = StaticEnum<ENiagaraScriptUsage>()->GetValueByNameString(Module->GetStringField(TEXT("usage")));
        UNiagaraNodeOutput* Output = UsageValue == INDEX_NONE ? nullptr
            : Graph.FindEquivalentOutputNode(static_cast<ENiagaraScriptUsage>(UsageValue));
        if (!Output)
        {
            Error = TEXT("Missing native stack output: ") + Module->GetStringField(TEXT("usage"));
            return false;
        }
        UNiagaraNodeFunctionCall* Function = nullptr;
        const TArray<TSharedPtr<FJsonValue>>* Assignments;
        if (Module->TryGetArrayField(TEXT("set"), Assignments))
        {
            TArray<FNiagaraVariable> Variables;
            TArray<FString> Defaults;
            for (const auto& Assignment : *Assignments)
            {
                const FObject Value = Assignment->AsObject();
                const FNiagaraTypeDefinition Type = InputType(Value->GetStringField(TEXT("type")));
                if (!Type.IsValid())
                {
                    Error = TEXT("Unsupported Niagara assignment type");
                    return false;
                }
                const FString Default = Value->GetStringField(TEXT("value"));
                if (!ValidColorDefault(Type, Default))
                {
                    Error = TEXT("Invalid native assignment color default");
                    return false;
                }
                Variables.Emplace(Type, FName(*Value->GetStringField(TEXT("name"))));
                Defaults.Add(Default);
            }
            Function = FNiagaraStackGraphUtilities::AddParameterModuleToStack(Variables, *Output, InsertIndex, Defaults);
        }
        else
        {
            const FString Path = Module->GetStringField(TEXT("script"));
            UNiagaraScript* Script = LoadObject<UNiagaraScript>(nullptr, *Path);
            if (!Script)
            {
                Error = TEXT("Missing native module: ") + Path;
                return false;
            }
            // The native system factory already inserts System State.
            if (UsageValue == static_cast<int64>(ENiagaraScriptUsage::SystemUpdateScript))
            {
                for (UEdGraphNode* Node : Graph.Nodes)
                {
                    UNiagaraNodeFunctionCall* Existing = Cast<UNiagaraNodeFunctionCall>(Node);
                    if (Existing && Existing->FunctionScript == Script) { Function = Existing; break; }
                }
            }
            if (!Function)
            {
                FString Name;
                Module->TryGetStringField(TEXT("name"), Name);
                Function = FNiagaraStackGraphUtilities::AddScriptModuleToStack(Script, *Output, InsertIndex, Name);
            }
        }
        const ENiagaraScriptUsage Usage = static_cast<ENiagaraScriptUsage>(UsageValue);
        const FCompileConstantResolver Resolver = Emitter.Emitter
            ? FCompileConstantResolver(Emitter, Usage) : FCompileConstantResolver(System, Usage);
        if (!Function || !ConfigureFunction(*Function, Module, Resolver, Error)) return false;
        if (InsertIndex != INDEX_NONE) ++InsertIndex;
    }
    Graph.NotifyGraphChanged();
    return true;
}

FObject DescribePin(const UEdGraphPin& Pin)
{
    FObject Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("name"), Pin.PinName.ToString());
    Result->SetStringField(TEXT("direction"), Pin.Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
    Result->SetStringField(TEXT("default"), Pin.DefaultValue);
    const FNiagaraTypeDefinition Type = UEdGraphSchema_Niagara::PinToTypeDefinition(&Pin);
    Result->SetStringField(TEXT("type"), Type.GetName());
    if (Pin.LinkedTo.IsEmpty()) DescribeColorDefault(Result, Type, Pin.DefaultValue);
    TArray<TSharedPtr<FJsonValue>> Links;
    for (const UEdGraphPin* Linked : Pin.LinkedTo)
    {
        FObject Link = MakeShared<FJsonObject>();
        Link->SetStringField(TEXT("pin"), Linked->PinName.ToString());
        Link->SetStringField(TEXT("node"), Linked->GetOwningNode()->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
        if (const UNiagaraNodeInput* Input = Cast<UNiagaraNodeInput>(Linked->GetOwningNode()))
        {
            const FObjectPropertyBase* InterfaceProperty = FindFProperty<FObjectPropertyBase>(Input->GetClass(), TEXT("DataInterface"));
            if (const UNiagaraDataInterfaceCurveBase* Curve = Cast<UNiagaraDataInterfaceCurveBase>(
                InterfaceProperty ? InterfaceProperty->GetObjectPropertyValue_InContainer(Input) : nullptr))
            {
                Link->SetStringField(TEXT("curve_asset"), GetPathNameSafe(Curve->CurveAsset));
                Link->SetBoolField(TEXT("use_lut"), Curve->bUseLUT);
                Link->SetBoolField(TEXT("optimize_lut"), Curve->bOptimizeLUT);
                bool bMatches = false;
                if (const UNiagaraDataInterfaceCurve* FloatCurve = Cast<UNiagaraDataInterfaceCurve>(Curve))
                    if (const UCurveFloat* Asset = Cast<UCurveFloat>(Curve->CurveAsset))
                        bMatches = FloatCurve->Curve == Asset->FloatCurve;
                if (const UNiagaraDataInterfaceColorCurve* ColorCurve = Cast<UNiagaraDataInterfaceColorCurve>(Curve))
                    if (const UCurveLinearColor* Asset = Cast<UCurveLinearColor>(Curve->CurveAsset))
                        bMatches = ColorCurve->RedCurve == Asset->FloatCurves[0]
                            && ColorCurve->GreenCurve == Asset->FloatCurves[1]
                            && ColorCurve->BlueCurve == Asset->FloatCurves[2]
                            && ColorCurve->AlphaCurve == Asset->FloatCurves[3];
                if (const UNiagaraDataInterfaceVectorCurve* VectorCurve = Cast<UNiagaraDataInterfaceVectorCurve>(Curve))
                    if (const UCurveVector* Asset = Cast<UCurveVector>(Curve->CurveAsset))
                        bMatches = VectorCurve->XCurve == Asset->FloatCurves[0]
                            && VectorCurve->YCurve == Asset->FloatCurves[1]
                            && VectorCurve->ZCurve == Asset->FloatCurves[2];
                if (const UNiagaraDataInterfaceVector2DCurve* VectorCurve = Cast<UNiagaraDataInterfaceVector2DCurve>(Curve))
                    if (const UCurveVector* Asset = Cast<UCurveVector>(Curve->CurveAsset))
                        bMatches = VectorCurve->XCurve == Asset->FloatCurves[0]
                            && VectorCurve->YCurve == Asset->FloatCurves[1];
                Link->SetBoolField(TEXT("curve_matches_asset"), bMatches);
            }
        }
        Links.Add(MakeShared<FJsonValueObject>(Link));
    }
    Result->SetArrayField(TEXT("links"), Links);
    if (const UEnum* Enum = Type.GetEnum())
    {
        Result->SetStringField(TEXT("enum"), Enum->GetPathName());
        TArray<TSharedPtr<FJsonValue>> Values;
        for (int32 Index = 0; Index < Enum->NumEnums(); ++Index)
        {
            FObject Value = MakeShared<FJsonObject>();
            Value->SetStringField(TEXT("name"), Enum->GetNameStringByIndex(Index));
            Value->SetStringField(TEXT("display_name"), Enum->GetDisplayNameTextByIndex(Index).ToString());
            Value->SetNumberField(TEXT("value"), Enum->GetValueByIndex(Index));
            Values.Add(MakeShared<FJsonValueObject>(Value));
        }
        Result->SetArrayField(TEXT("enum_values"), Values);
    }
    return Result;
}

TArray<TSharedPtr<FJsonValue>> DescribeGraph(UNiagaraGraph* Graph)
{
    TArray<TSharedPtr<FJsonValue>> Functions;
    if (!Graph) return Functions;
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        UNiagaraNodeFunctionCall* Function = Cast<UNiagaraNodeFunctionCall>(Node);
        if (!Function && Node->GetClass()->GetFName() != TEXT("NiagaraNodeParameterMapSet")) continue;
        FObject Value = MakeShared<FJsonObject>();
        Value->SetStringField(TEXT("name"), Function ? Function->GetFunctionName() : TEXT("ParameterOverrides"));
        Value->SetStringField(TEXT("script"), Function ? GetPathNameSafe(Function->FunctionScript) : FString());
        Value->SetStringField(TEXT("usage"), StaticEnum<ENiagaraScriptUsage>()->GetNameStringByValue(
            static_cast<int64>(FNiagaraStackGraphUtilities::GetOutputNodeUsage(*CastChecked<UNiagaraNode>(Node)))));
        TArray<TSharedPtr<FJsonValue>> Pins;
        for (UEdGraphPin* Pin : Node->Pins) Pins.Add(MakeShared<FJsonValueObject>(DescribePin(*Pin)));
        Value->SetArrayField(TEXT("pins"), Pins);
        if (const UNiagaraNodeAssignment* Assignment = Cast<UNiagaraNodeAssignment>(Node))
        {
            TArray<TSharedPtr<FJsonValue>> Assignments;
            const TArray<FNiagaraVariable>& Targets = Assignment->GetAssignmentTargets();
            const TArray<FString>& Defaults = Assignment->GetAssignmentDefaults();
            for (int32 Index = 0; Index < Targets.Num(); ++Index)
            {
                FObject Entry = MakeShared<FJsonObject>();
                Entry->SetStringField(TEXT("name"), Targets[Index].GetName().ToString());
                Entry->SetStringField(TEXT("type"), Targets[Index].GetType().GetName());
                Entry->SetStringField(TEXT("default"), Defaults.IsValidIndex(Index) ? Defaults[Index] : FString());
                DescribeColorDefault(Entry, Targets[Index].GetType(), Defaults.IsValidIndex(Index) ? Defaults[Index] : FString());
                Assignments.Add(MakeShared<FJsonValueObject>(Entry));
            }
            Value->SetArrayField(TEXT("assignments"), Assignments);
        }
        Functions.Add(MakeShared<FJsonValueObject>(Value));
    }
    return Functions;
}
}

FString UNiagaraWeaponAuthoring::DescribeModuleScript(const FString& ScriptPath)
{
    FObject Result = MakeShared<FJsonObject>();
    UNiagaraScript* Script = LoadObject<UNiagaraScript>(nullptr, *ScriptPath);
    if (!Script) { Result->SetStringField(TEXT("error"), TEXT("Missing script")); return SerializeJSON(Result); }
    Result->SetStringField(TEXT("script"), Script->GetPathName());
    UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(Script->GetLatestSource());
    TArray<TSharedPtr<FJsonValue>> Inputs;
    TSet<FName> Names;
    if (Source && Source->NodeGraph)
    {
        for (UEdGraphNode* Node : Source->NodeGraph->Nodes)
        {
            for (UEdGraphPin* Pin : Node->Pins)
            {
                if (Pin->Direction == EGPD_Output && Pin->PinName.ToString().StartsWith(TEXT("Module.")) && !Names.Contains(Pin->PinName))
                {
                    Names.Add(Pin->PinName);
                    FObject Input = DescribePin(*Pin);
                    const TOptional<FNiagaraVariableMetaData> Metadata = Source->NodeGraph->GetMetaData(
                        FNiagaraVariable(UEdGraphSchema_Niagara::PinToTypeDefinition(Pin), Pin->PinName));
                    if (Metadata.IsSet())
                    {
                        Input->SetStringField(TEXT("description"), Metadata->Description.ToString());
                        Input->SetStringField(TEXT("display_unit"), StaticEnum<EUnit>()->GetNameStringByValue(static_cast<int64>(Metadata->DisplayUnit)));
                    }
                    for (UEdGraphPin* Default : Node->Pins)
                    {
                        if (Default->Direction == EGPD_Input && Default->PersistentGuid == Pin->PersistentGuid)
                            Input->SetStringField(TEXT("default"), Default->DefaultValue);
                    }
                    Inputs.Add(MakeShared<FJsonValueObject>(Input));
                }
            }
        }
    }
    Result->SetArrayField(TEXT("inputs"), Inputs);
    TArray<TSharedPtr<FJsonValue>> Nodes;
    if (Source && Source->NodeGraph)
        for (const UEdGraphNode* Node : Source->NodeGraph->Nodes)
        {
            FObject Entry = MakeShared<FJsonObject>();
            Entry->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
            Entry->SetStringField(TEXT("id"), Node->NodeGuid.ToString());
            if (const UNiagaraNodeFunctionCall* Function = Cast<UNiagaraNodeFunctionCall>(Node))
                Entry->SetStringField(TEXT("script"), GetPathNameSafe(Function->FunctionScript));
            TArray<TSharedPtr<FJsonValue>> Pins;
            for (const UEdGraphPin* Pin : Node->Pins) Pins.Add(MakeShared<FJsonValueObject>(DescribePin(*Pin)));
            Entry->SetArrayField(TEXT("pins"), Pins);
            Nodes.Add(MakeShared<FJsonValueObject>(Entry));
        }
    Result->SetArrayField(TEXT("native_nodes"), Nodes);
    UNiagaraEmitter* Probe = NewObject<UNiagaraEmitter>(GetTransientPackage());
    UNiagaraEmitterFactoryNew::InitializeEmitter(Probe, false);
    UNiagaraGraph* Graph = CastChecked<UNiagaraScriptSource>(Probe->GetLatestEmitterData()->GraphSource)->NodeGraph;
    UNiagaraNodeOutput* Output = Graph->FindEquivalentOutputNode(ENiagaraScriptUsage::ParticleSpawnScript);
    UNiagaraNodeFunctionCall* Function = FNiagaraStackGraphUtilities::AddScriptModuleToStack(Script, *Output);
    TArray<TSharedPtr<FJsonValue>> Switches;
    if (Function)
        for (UEdGraphPin* Pin : Function->Pins)
            if (Pin->Direction == EGPD_Input && Pin->PinName != TEXT("InputMap"))
                Switches.Add(MakeShared<FJsonValueObject>(DescribePin(*Pin)));
    Result->SetArrayField(TEXT("switches"), Switches);
    return SerializeJSON(Result);
}

UNiagaraSystem* UNiagaraWeaponAuthoring::CreateWeaponSystem(const FString& PackagePath, const FString& SpecificationJSON)
{
    FObject Spec;
    if (!PackagePath.StartsWith(TEXT("/Game/WuWaShowcase/Characters/")) || !PackagePath.Contains(TEXT("/Weapons/Effects/"))
        || !FPackageName::IsValidLongPackageName(PackagePath) || FPackageName::DoesPackageExist(PackagePath)
        || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(SpecificationJSON), Spec) || !Spec.IsValid()) return nullptr;
    UPackage* Package = CreatePackage(*PackagePath);
    UNiagaraSystem* System = NewObject<UNiagaraSystem>(Package, FName(*FPackageName::GetLongPackageAssetName(PackagePath)), RF_Public | RF_Standalone | RF_Transactional);
    UNiagaraSystemFactoryNew::InitializeSystem(System, true);
    FString Error;
    UNiagaraGraph* Graph = CastChecked<UNiagaraScriptSource>(System->GetSystemSpawnScript()->GetLatestSource())->NodeGraph;
    const TArray<TSharedPtr<FJsonValue>>* Parameters;
    if (Spec->TryGetArrayField(TEXT("user_floats"), Parameters))
    {
        for (const auto& Item : *Parameters)
        {
            const FObject Parameter = Item->AsObject();
            const FNiagaraVariable Variable(FNiagaraTypeDefinition::GetFloatDef(), FName(*Parameter->GetStringField(TEXT("name"))));
            System->GetExposedParameters().AddParameter(Variable, true);
            System->GetExposedParameters().SetParameterValue<float>(Parameter->GetNumberField(TEXT("value")), Variable);
        }
    }
    bool bSuccess = AddModules(*Graph, Spec, System, FVersionedNiagaraEmitter(), Error);
    for (const auto& Item : Spec->GetArrayField(TEXT("emitters")))
    {
        if (!bSuccess) break;
        const FObject Settings = Item->AsObject();
        const FString Name = Settings->GetStringField(TEXT("name"));
        UNiagaraEmitter* Emitter = NewObject<UNiagaraEmitter>(System, FName(*Name), RF_Transactional);
        UNiagaraEmitterFactoryNew::InitializeEmitter(Emitter, false);
        Emitter->SetUniqueEmitterName(Name);
        FVersionedNiagaraEmitterData* Data = Emitter->GetLatestEmitterData();
        Data->bLocalSpace = Settings->GetBoolField(TEXT("local_space"));
        Data->bDeterminism = Settings->GetBoolField(TEXT("determinism"));
        Data->RandomSeed = Settings->GetIntegerField(TEXT("random_seed"));
        Data->bRequiresPersistentIDs = Settings->GetBoolField(TEXT("persistent_ids"));
        Data->InterpolatedSpawnMode = Settings->GetBoolField(TEXT("interpolated_spawn"))
            ? ENiagaraInterpolatedSpawnMode::Interpolation : ENiagaraInterpolatedSpawnMode::NoInterpolation;
        if (Settings->GetBoolField(TEXT("interpolated_spawn")))
            Data->SpawnScriptProps.Script->SetUsage(ENiagaraScriptUsage::ParticleSpawnScriptInterpolated);
        UNiagaraGraph* EmitterGraph = CastChecked<UNiagaraScriptSource>(Data->GraphSource)->NodeGraph;
        bSuccess = AddModules(*EmitterGraph, Settings, nullptr,
            FVersionedNiagaraEmitter(Emitter, Emitter->GetExposedVersion().VersionGuid), Error);
        if (!bSuccess) break;
        for (const auto& RendererValue : Settings->GetArrayField(TEXT("renderers")))
        {
            const FObject RendererSpec = RendererValue->AsObject();
            UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *RendererSpec->GetStringField(TEXT("material")));
            if (!Material) { Error = TEXT("Missing native renderer material"); bSuccess = false; break; }
            FString Kind = TEXT("mesh");
            RendererSpec->TryGetStringField(TEXT("kind"), Kind);
            UNiagaraRendererProperties* Renderer = nullptr;
            FNiagaraVariableAttributeBinding* ColorBinding = nullptr;
            FNiagaraRendererMaterialParameters* MaterialParameters = nullptr;
            FString FacingMode;
            if (Kind == TEXT("mesh"))
            {
                UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *RendererSpec->GetStringField(TEXT("mesh")));
                if (!Mesh) { Error = TEXT("Missing native renderer mesh"); bSuccess = false; break; }
                UNiagaraMeshRendererProperties* MeshRenderer = NewObject<UNiagaraMeshRendererProperties>(Emitter, NAME_None, RF_Transactional);
                FNiagaraMeshRendererMeshProperties MeshEntry;
                MeshEntry.Mesh = Mesh;
                const TArray<TSharedPtr<FJsonValue>>* PivotOffset;
                if (RendererSpec->TryGetArrayField(TEXT("pivot_offset"), PivotOffset))
                {
                    if (PivotOffset->Num() != 3)
                    { Error = TEXT("Invalid native mesh pivot offset"); bSuccess = false; break; }
                    MeshEntry.PivotOffset = FVector((*PivotOffset)[0]->AsNumber(), (*PivotOffset)[1]->AsNumber(), (*PivotOffset)[2]->AsNumber());
                }
                FString PivotSpace;
                if (RendererSpec->TryGetStringField(TEXT("pivot_offset_space"), PivotSpace))
                {
                    const int64 Space = StaticEnum<ENiagaraMeshPivotOffsetSpace>()->GetValueByNameString(PivotSpace);
                    if (Space == INDEX_NONE) { Error = TEXT("Invalid native mesh pivot space"); bSuccess = false; break; }
                    MeshEntry.PivotOffsetSpace = static_cast<ENiagaraMeshPivotOffsetSpace>(Space);
                }
                MeshRenderer->Meshes.Reset();
                MeshRenderer->Meshes.Add(MeshEntry);
                MeshRenderer->bOverrideMaterials = true;
                FNiagaraMeshMaterialOverride Override;
                Override.ExplicitMat = Material;
                MeshRenderer->OverrideMaterials.Add(Override);
                if (RendererSpec->TryGetStringField(TEXT("facing_mode"), FacingMode))
                {
                    const int64 Mode = StaticEnum<ENiagaraMeshFacingMode>()->GetValueByNameString(FacingMode);
                    if (Mode == INDEX_NONE) { Error = TEXT("Invalid native mesh facing mode"); bSuccess = false; break; }
                    MeshRenderer->FacingMode = static_cast<ENiagaraMeshFacingMode>(Mode);
                }
                Renderer = MeshRenderer;
                bool bLockedAxisEnable = false;
                if (RendererSpec->TryGetBoolField(TEXT("locked_axis_enable"), bLockedAxisEnable))
                {
                    MeshRenderer->bLockedAxisEnable = bLockedAxisEnable;
                }
                const TArray<TSharedPtr<FJsonValue>>* LockedAxis;
                if (RendererSpec->TryGetArrayField(TEXT("locked_axis"), LockedAxis))
                {
                    if (LockedAxis->Num() != 3)
                    { Error = TEXT("Invalid native mesh locked axis"); bSuccess = false; break; }
                    MeshRenderer->LockedAxis = FVector((*LockedAxis)[0]->AsNumber(), (*LockedAxis)[1]->AsNumber(), (*LockedAxis)[2]->AsNumber());
                }
                ColorBinding = &MeshRenderer->ColorBinding;
                MaterialParameters = &MeshRenderer->MaterialParameters;
                FString ScaleVariable;
                if (RendererSpec->TryGetStringField(TEXT("scale_binding"), ScaleVariable))
                {
                    if (!ScaleVariable.StartsWith(TEXT("Particles.")))
                    { Error = TEXT("Unsupported renderer scale binding: ") + ScaleVariable; bSuccess = false; break; }
                    MeshRenderer->ScaleBinding.SetValue(FName(*ScaleVariable),
                        FVersionedNiagaraEmitterBase(Emitter, Emitter->GetExposedVersion().VersionGuid),
                        ENiagaraRendererSourceDataMode::Particles);
                }
            }
            else if (Kind == TEXT("sprite"))
            {
                UNiagaraSpriteRendererProperties* SpriteRenderer = NewObject<UNiagaraSpriteRendererProperties>(Emitter, NAME_None, RF_Transactional);
                SpriteRenderer->Material = Material;
                const TArray<TSharedPtr<FJsonValue>>* PivotInUVSpace;
                if (RendererSpec->TryGetArrayField(TEXT("pivot_in_uv_space"), PivotInUVSpace))
                {
                    if (PivotInUVSpace->Num() != 2)
                    { Error = TEXT("Invalid native sprite pivot"); bSuccess = false; break; }
                    SpriteRenderer->PivotInUVSpace = FVector2D((*PivotInUVSpace)[0]->AsNumber(), (*PivotInUVSpace)[1]->AsNumber());
                }
                if (RendererSpec->TryGetStringField(TEXT("facing_mode"), FacingMode))
                {
                    const int64 Mode = StaticEnum<ENiagaraSpriteFacingMode>()->GetValueByNameString(FacingMode);
                    if (Mode == INDEX_NONE) { Error = TEXT("Invalid native sprite facing mode"); bSuccess = false; break; }
                    SpriteRenderer->FacingMode = static_cast<ENiagaraSpriteFacingMode>(Mode);
                }
                const TArray<TSharedPtr<FJsonValue>>* SubImages;
                if (RendererSpec->TryGetArrayField(TEXT("sub_image_size"), SubImages))
                {
                    if (SubImages->Num() != 2 || (*SubImages)[0]->AsNumber() < 1. || (*SubImages)[1]->AsNumber() < 1.)
                    { Error = TEXT("Invalid native sprite sub-image size"); bSuccess = false; break; }
                    SpriteRenderer->SubImageSize = FVector2D((*SubImages)[0]->AsNumber(), (*SubImages)[1]->AsNumber());
                }
                bool bBlend = false;
                if (RendererSpec->TryGetBoolField(TEXT("sub_image_blend"), bBlend)) SpriteRenderer->bSubImageBlend = bBlend;
                Renderer = SpriteRenderer;
                ColorBinding = &SpriteRenderer->ColorBinding;
                MaterialParameters = &SpriteRenderer->MaterialParameters;
            }
            else { Error = TEXT("Unsupported native renderer kind: ") + Kind; bSuccess = false; break; }
            FString SourceModeName;
            if (RendererSpec->TryGetStringField(TEXT("source_mode"), SourceModeName))
            {
                const int64 Mode = StaticEnum<ENiagaraRendererSourceDataMode>()->GetValueByNameString(SourceModeName);
                if (Mode == INDEX_NONE) { Error = TEXT("Invalid native renderer source mode"); bSuccess = false; break; }
                const auto SourceMode = static_cast<ENiagaraRendererSourceDataMode>(Mode);
                if (auto* MeshRenderer = Cast<UNiagaraMeshRendererProperties>(Renderer)) MeshRenderer->SourceMode = SourceMode;
                if (auto* SpriteRenderer = Cast<UNiagaraSpriteRendererProperties>(Renderer)) SpriteRenderer->SourceMode = SourceMode;
                FPropertyChangedEvent Change(FindFProperty<FProperty>(Renderer->GetClass(), TEXT("SourceMode")));
                Renderer->PostEditChangeProperty(Change);
            }
            const FObject* AttributeBindings;
            if (RendererSpec->TryGetObjectField(TEXT("attribute_bindings"), AttributeBindings))
            {
                for (const auto& Pair : (*AttributeBindings)->Values)
                {
                    FStructProperty* Property = FindFProperty<FStructProperty>(Renderer->GetClass(), FName(*Pair.Key));
                    const FString Variable = Pair.Value->AsString();
                    if (!Property || Property->Struct != FNiagaraVariableAttributeBinding::StaticStruct()
                        || (!Variable.StartsWith(TEXT("Particles.")) && !Variable.StartsWith(TEXT("Emitter."))
                            && !Variable.StartsWith(TEXT("System.")) && !Variable.StartsWith(TEXT("User."))))
                    { Error = FString(TEXT("Unsupported native renderer attribute binding: ")) + FString(Pair.Key); bSuccess = false; break; }
                    Property->ContainerPtrToValuePtr<FNiagaraVariableAttributeBinding>(Renderer)->SetValue(FName(*Variable),
                        FVersionedNiagaraEmitterBase(Emitter, Emitter->GetExposedVersion().VersionGuid), Renderer->GetCurrentSourceMode());
                }
                if (!bSuccess) break;
            }
            Renderer->SortOrderHint = RendererSpec->GetIntegerField(TEXT("sort_order"));
            FString MotionVectorSetting;
            if (RendererSpec->TryGetStringField(TEXT("motion_vector_setting"), MotionVectorSetting))
            {
                const int64 Setting = StaticEnum<ENiagaraRendererMotionVectorSetting>()->GetValueByNameString(MotionVectorSetting);
                if (Setting == INDEX_NONE) { Error = TEXT("Invalid native renderer motion vector setting"); bSuccess = false; break; }
                Renderer->MotionVectorSetting = static_cast<ENiagaraRendererMotionVectorSetting>(Setting);
            }
            FString ColorVariable;
            if (RendererSpec->TryGetStringField(TEXT("color_binding"), ColorVariable))
            {
                if (!ColorVariable.StartsWith(TEXT("Particles.")))
                {
                    Error = TEXT("Unsupported renderer color binding: ") + ColorVariable;
                    bSuccess = false;
                    break;
                }
                ColorBinding->SetValue(FName(*ColorVariable),
                    FVersionedNiagaraEmitterBase(Emitter, Emitter->GetExposedVersion().VersionGuid),
                    ENiagaraRendererSourceDataMode::Particles);
            }
            const FObject* MaterialFloats;
            if (RendererSpec->TryGetObjectField(TEXT("material_float_bindings"), MaterialFloats))
            {
                for (const auto& Pair : (*MaterialFloats)->Values)
                {
                    const FString VariableName = Pair.Value->AsString();
                    if (!VariableName.StartsWith(TEXT("Emitter.")) && !VariableName.StartsWith(TEXT("System."))
                        && !VariableName.StartsWith(TEXT("User.")))
                    {
                        Error = TEXT("Unsupported renderer material float binding: ") + VariableName;
                        bSuccess = false;
                        break;
                    }
                    FNiagaraMaterialAttributeBinding Binding;
                    Binding.MaterialParameterName = FName(*Pair.Key);
                    Binding.NiagaraVariable = FNiagaraVariableBase(FNiagaraTypeDefinition::GetFloatDef(), FName(*VariableName));
                    Binding.CacheValues(Emitter);
                    MaterialParameters->AttributeBindings.Add(Binding);
                }
                if (!bSuccess) break;
            }
            Emitter->AddRenderer(Renderer, Emitter->GetExposedVersion().VersionGuid);
        }
        if (bSuccess) FNiagaraEditorUtilities::AddEmitterToSystem(*System, *Emitter, Emitter->GetExposedVersion().VersionGuid, false);
    }
    if (!bSuccess)
    {
        UE_LOG(LogTemp, Error, TEXT("Native weapon Niagara authoring failed: %s"), *Error);
        System->ClearFlags(RF_Public | RF_Standalone);
        return nullptr;
    }
    System->MarkPackageDirty();
    return System;
}

FString UNiagaraWeaponAuthoring::SetWeaponGraphDefaults(UNiagaraSystem* System, const FString& ChangesJSON)
{
    FObject Result = MakeShared<FJsonObject>();
    FObject Changes;
    const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
    if (!System || !System->GetPathName().StartsWith(TEXT("/Game/WuWaShowcase/Characters/"))
        || !System->GetPathName().Contains(TEXT("/Weapons/Effects/"))
        || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ChangesJSON), Changes)
        || !Changes.IsValid() || !Changes->TryGetArrayField(TEXT("changes"), Entries) || Entries->IsEmpty())
    {
        Result->SetStringField(TEXT("error"), TEXT("Expected a weapon effect and nonempty changes array"));
        return SerializeJSON(Result);
    }
    System->EnsureFullyLoaded();
    TArray<UEdGraphPin*> Pins;
    TArray<FString> Values;
    TMap<UNiagaraNodeAssignment*, TMap<int32, FString>> AssignmentValues;
    for (const TSharedPtr<FJsonValue>& Entry : *Entries)
    {
        const FObject Change = Entry->AsObject();
        FString EmitterName, FunctionName, ScriptPath, Usage, PinName, Expected, Value;
        if (!Change.IsValid() || !Change->TryGetStringField(TEXT("emitter"), EmitterName)
            || !Change->TryGetStringField(TEXT("function"), FunctionName)
            || !Change->TryGetStringField(TEXT("script"), ScriptPath)
            || !Change->TryGetStringField(TEXT("usage"), Usage)
            || !Change->TryGetStringField(TEXT("pin"), PinName)
            || !Change->TryGetStringField(TEXT("expected"), Expected)
            || !Change->TryGetStringField(TEXT("value"), Value))
        {
            Result->SetStringField(TEXT("error"), TEXT("Incomplete graph default change"));
            return SerializeJSON(Result);
        }
        bool bAssignment = false;
        Change->TryGetBoolField(TEXT("assignment"), bAssignment);
        TArray<UEdGraphPin*> Matches;
        TArray<UNiagaraNodeAssignment*> AssignmentMatches;
        for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
        {
            if (Handle.GetName().ToString() != EmitterName) continue;
            UNiagaraGraph* Graph = CastChecked<UNiagaraScriptSource>(Handle.GetEmitterData()->GraphSource)->NodeGraph;
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                UNiagaraNodeFunctionCall* Function = Cast<UNiagaraNodeFunctionCall>(Node);
                if (Function ? Function->GetFunctionName() != FunctionName
                    || GetPathNameSafe(Function->FunctionScript) != ScriptPath
                    : FunctionName != TEXT("ParameterOverrides") || !ScriptPath.IsEmpty()
                    || Node->GetClass()->GetFName() != TEXT("NiagaraNodeParameterMapSet")) continue;
                UNiagaraNode* NiagaraNode = CastChecked<UNiagaraNode>(Node);
                if (StaticEnum<ENiagaraScriptUsage>()->GetNameStringByValue(static_cast<int64>(
                    FNiagaraStackGraphUtilities::GetOutputNodeUsage(*NiagaraNode))) != Usage) continue;
                if (bAssignment)
                {
                    if (UNiagaraNodeAssignment* Assignment = Cast<UNiagaraNodeAssignment>(Node))
                        if (Assignment->FindAssignmentTarget(FName(*PinName)) != INDEX_NONE)
                            AssignmentMatches.Add(Assignment);
                }
                else if (UEdGraphPin* Pin = Node->FindPin(FName(*PinName), EGPD_Input)) Matches.Add(Pin);
            }
        }
        if (bAssignment)
        {
            if (AssignmentMatches.Num() != 1)
            {
                Result->SetStringField(TEXT("error"), TEXT("Assignment target is not unique: ") + PinName);
                return SerializeJSON(Result);
            }
            UNiagaraNodeAssignment* Assignment = AssignmentMatches[0];
            const int32 Index = Assignment->FindAssignmentTarget(FName(*PinName));
            if (!Assignment->GetAssignmentDefaults().IsValidIndex(Index)
                || Assignment->GetAssignmentDefaults()[Index] != Expected
                || AssignmentValues.FindOrAdd(Assignment).Contains(Index)
                || !ValidColorDefault(Assignment->GetAssignmentTarget(Index).GetType(), Value))
            {
                Result->SetStringField(TEXT("error"), TEXT("Assignment default precondition differs: ") + PinName);
                return SerializeJSON(Result);
            }
            AssignmentValues[Assignment].Add(Index, Value);
            continue;
        }
        if (Matches.Num() != 1 || Pins.Contains(Matches[0]) || !Matches[0]->LinkedTo.IsEmpty()
            || Matches[0]->DefaultValue != Expected
            || !ValidColorDefault(UEdGraphSchema_Niagara::PinToTypeDefinition(Matches[0]), Value))
        {
            Result->SetStringField(TEXT("error"), TEXT("Graph default precondition differs: ")
                + EmitterName + TEXT(".") + FunctionName + TEXT(".") + PinName);
            return SerializeJSON(Result);
        }
        Pins.Add(Matches[0]);
        Values.Add(Value);
    }
    System->Modify();
    const UEdGraphSchema_Niagara* Schema = GetDefault<UEdGraphSchema_Niagara>();
    for (int32 Index = 0; Index < Pins.Num(); ++Index)
    {
        UEdGraphNode* Node = Pins[Index]->GetOwningNode();
        Node->Modify();
        Node->GetGraph()->Modify();
        Schema->TrySetDefaultValue(*Pins[Index], Values[Index]);
        Node->GetGraph()->NotifyGraphChanged();
    }
    int32 Changed = Pins.Num();
    for (const auto& Pair : AssignmentValues)
    {
        UNiagaraNodeAssignment* Assignment = Pair.Key;
        Assignment->Modify();
        Assignment->GetGraph()->Modify();
        for (const auto& Default : Pair.Value)
        {
            Assignment->SetAssignmentTarget(Default.Key, Assignment->GetAssignmentTarget(Default.Key), &Default.Value);
            ++Changed;
        }
        Assignment->RefreshFromExternalChanges();
        Assignment->GetGraph()->NotifyGraphChanged();
    }
    System->MarkPackageDirty();
    Result->SetNumberField(TEXT("changed"), Changed);
    return SerializeJSON(Result);
}

FString UNiagaraWeaponAuthoring::MoveWeaponModuleBefore(UNiagaraSystem* System, const FString& EmitterName,
    const FString& UsageName, const FString& ModuleName, const FString& BeforeModuleName)
{
    FObject Result = MakeShared<FJsonObject>();
    const int64 UsageValue = StaticEnum<ENiagaraScriptUsage>()->GetValueByNameString(UsageName);
    if (!System || !System->GetPathName().StartsWith(TEXT("/Game/WuWaShowcase/Characters/"))
        || !System->GetPathName().Contains(TEXT("/Weapons/Effects/"))
        || (UsageValue != static_cast<int64>(ENiagaraScriptUsage::ParticleSpawnScript)
            && UsageValue != static_cast<int64>(ENiagaraScriptUsage::ParticleUpdateScript)))
    {
        Result->SetStringField(TEXT("error"), TEXT("Expected a weapon particle stack"));
        return SerializeJSON(Result);
    }
    System->EnsureFullyLoaded();
    for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
    {
        if (Handle.GetName().ToString() != EmitterName) continue;
        UNiagaraGraph* Graph = CastChecked<UNiagaraScriptSource>(Handle.GetEmitterData()->GraphSource)->NodeGraph;
        const ENiagaraScriptUsage Usage = static_cast<ENiagaraScriptUsage>(UsageValue);
        UNiagaraNodeOutput* Output = Graph->FindEquivalentOutputNode(Usage);
        TArray<UNiagaraNodeFunctionCall*> Modules;
        if (Output) OrderedModules(*Output, Modules);
        const int32 MoveIndex = Modules.IndexOfByPredicate([&](const UNiagaraNodeFunctionCall* Node)
            { return Node->GetFunctionName() == ModuleName; });
        const int32 BeforeIndex = Modules.IndexOfByPredicate([&](const UNiagaraNodeFunctionCall* Node)
            { return Node->GetFunctionName() == BeforeModuleName; });
        if (!Output || BeforeIndex <= 0 || MoveIndex != BeforeIndex + 1)
        {
            Result->SetStringField(TEXT("error"), TEXT("Expected adjacent modules in the original order"));
            return SerializeJSON(Result);
        }
        UNiagaraNodeFunctionCall* Module = Modules[MoveIndex];
        // Preserve the complete parameter-map fanout, including dynamic-input branches,
        // at each group boundary when swapping the two adjacent native modules.
        UEdGraphPin* Previous = ParameterMapPin(*Modules[BeforeIndex - 1], EGPD_Output);
        UEdGraphPin* Before = ParameterMapPin(*Modules[BeforeIndex], EGPD_Output);
        UEdGraphPin* Moving = ParameterMapPin(*Module, EGPD_Output);
        if (!Previous || !Before || !Moving)
        {
            Result->SetStringField(TEXT("error"), TEXT("Missing parameter-map group boundary"));
            return SerializeJSON(Result);
        }
        const TArray<UEdGraphPin*> BeforeInputs = Previous->LinkedTo;
        const TArray<UEdGraphPin*> MovingInputs = Before->LinkedTo;
        const TArray<UEdGraphPin*> NextInputs = Moving->LinkedTo;
        TSet<UEdGraphPin*> Inputs;
        for (const TArray<UEdGraphPin*>* Group : { &BeforeInputs, &MovingInputs, &NextInputs })
        {
            if (Group->IsEmpty())
            {
                Result->SetStringField(TEXT("error"), TEXT("Empty parameter-map group boundary"));
                return SerializeJSON(Result);
            }
            for (UEdGraphPin* Pin : *Group)
            {
                if (!Pin || Pin->Direction != EGPD_Input || Pin->LinkedTo.Num() != 1
                    || Pin->GetOwningNode()->GetGraph() != Graph || Inputs.Contains(Pin)
                    || UEdGraphSchema_Niagara::PinToTypeDefinition(Pin) != FNiagaraTypeDefinition::GetParameterMapDef())
                {
                    Result->SetStringField(TEXT("error"), TEXT("Unexpected parameter-map group links"));
                    return SerializeJSON(Result);
                }
                Inputs.Add(Pin);
            }
        }
        System->Modify();
        Graph->Modify();
        for (UEdGraphPin* Pin : { Previous, Before, Moving }) Pin->GetOwningNode()->Modify();
        for (UEdGraphPin* Pin : Inputs) Pin->GetOwningNode()->Modify();
        for (UEdGraphPin* Pin : { Previous, Before, Moving }) Pin->BreakAllPinLinks(true);
        auto Connect = [](UEdGraphPin* From, const TArray<UEdGraphPin*>& ToPins)
        {
            for (UEdGraphPin* To : ToPins)
            {
                From->MakeLinkTo(To);
                From->GetOwningNode()->PinConnectionListChanged(From);
                To->GetOwningNode()->PinConnectionListChanged(To);
            }
        };
        Connect(Previous, MovingInputs);
        Connect(Moving, BeforeInputs);
        Connect(Before, NextInputs);
        Modules.Reset();
        OrderedModules(*Output, Modules);
        if (!Modules.IsValidIndex(BeforeIndex) || Modules[BeforeIndex] != Module)
        {
            Result->SetStringField(TEXT("error"), TEXT("Native module move did not retain the original node"));
            return SerializeJSON(Result);
        }
        Graph->NotifyGraphChanged();
        System->MarkPackageDirty();
        Result->SetNumberField(TEXT("moved"), 1);
        Result->SetNumberField(TEXT("from_index"), MoveIndex);
        Result->SetNumberField(TEXT("to_index"), BeforeIndex);
        return SerializeJSON(Result);
    }
    Result->SetStringField(TEXT("error"), TEXT("Emitter not found"));
    return SerializeJSON(Result);
}

FString UNiagaraWeaponAuthoring::ReplaceWeaponModule(UNiagaraSystem* System,
    const FString& EmitterName, const FString& UsageName, const FString& ModuleName,
    const FString& ExpectedScriptPath, const FString& ReplacementJSON)
{
    FObject Result = MakeShared<FJsonObject>();
    Result->SetNumberField(TEXT("replaced"), 0);
    FObject Replacement;
    FString ScriptPath;
    const int64 UsageValue = StaticEnum<ENiagaraScriptUsage>()->GetValueByNameString(UsageName);
    if (!System || !System->GetPathName().StartsWith(TEXT("/Game/WuWaShowcase/Characters/"))
        || !System->GetPathName().Contains(TEXT("/Weapons/Effects/")) || UsageValue == INDEX_NONE
        || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ReplacementJSON), Replacement)
        || !Replacement.IsValid() || !Replacement->TryGetStringField(TEXT("script"), ScriptPath)
        || !ScriptPath.StartsWith(TEXT("/Niagara/Modules/")))
    {
        Result->SetStringField(TEXT("error"), TEXT("Invalid native module replacement"));
        return SerializeJSON(Result);
    }
    UNiagaraScript* Script = LoadObject<UNiagaraScript>(nullptr, *ScriptPath);
    if (!Script)
    {
        Result->SetStringField(TEXT("error"), TEXT("Native replacement script not found"));
        return SerializeJSON(Result);
    }
    System->EnsureFullyLoaded();
    for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
    {
        if (Handle.GetName().ToString() != EmitterName || !Handle.GetEmitterData()) continue;
        const FVersionedNiagaraEmitter Emitter = Handle.GetInstance();
        UNiagaraGraph* Graph = CastChecked<UNiagaraScriptSource>(Handle.GetEmitterData()->GraphSource)->NodeGraph;
        const ENiagaraScriptUsage Usage = static_cast<ENiagaraScriptUsage>(UsageValue);
        UNiagaraNodeOutput* Output = Graph->FindEquivalentOutputNode(Usage);
        TArray<UNiagaraNodeFunctionCall*> Modules;
        if (Output) OrderedModules(*Output, Modules);
        TArray<int32> Matches;
        for (int32 Index = 0; Index < Modules.Num(); ++Index)
            if (Modules[Index]->GetFunctionName() == ModuleName
                && Modules[Index]->FunctionScript
                && Modules[Index]->FunctionScript->GetPathName() == ExpectedScriptPath) Matches.Add(Index);
        if (Matches.Num() != 1)
        {
            Result->SetStringField(TEXT("error"), TEXT("Expected native module is not unique"));
            return SerializeJSON(Result);
        }
        UNiagaraNodeFunctionCall* Function = Modules[Matches[0]];
        System->Modify();
        Graph->Modify();
        Function->Modify();
        Function->FunctionScript = Script;
        Function->SelectedScriptVersion = Script->IsVersioningEnabled()
            ? Script->GetExposedVersion().VersionGuid : FGuid();
        Function->RefreshFromExternalChanges();
        FNiagaraScriptVersionUpgradeContext Context;
        Context.ConstantResolver = FCompileConstantResolver(Emitter, Usage);
        Function->UpdateOverridePins(Context);
        FString Error;
        if (!ConfigureFunction(*Function, Replacement, Context.ConstantResolver, Error)
            || !AddModules(*Graph, Replacement, nullptr, Emitter, Error, Matches[0] + 1))
        {
            Result->SetStringField(TEXT("error"), Error);
            return SerializeJSON(Result);
        }
        Function->MarkNodeRequiresSynchronization(TEXT("Native module reassigned."), true);
        Graph->NotifyGraphChanged();
        System->MarkPackageDirty();
        Result->SetNumberField(TEXT("replaced"), 1);
        Result->SetNumberField(TEXT("module_index"), Matches[0]);
        Result->SetStringField(TEXT("retained_function_name"), Function->GetFunctionName());
        return SerializeJSON(Result);
    }
    Result->SetStringField(TEXT("error"), TEXT("Emitter not found"));
    return SerializeJSON(Result);
}

FString UNiagaraWeaponAuthoring::DescribeWeaponScriptBytecode(UNiagaraSystem* System,
    const FString& EmitterName, const FString& UsageName)
{
    FObject Result = MakeShared<FJsonObject>();
    if (!System) return SerializeJSON(Result);
    for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
    {
        if (Handle.GetName().ToString() != EmitterName || !Handle.GetEmitterData()) continue;
        TArray<UNiagaraScript*> Scripts;
        Handle.GetEmitterData()->GetScripts(Scripts, true);
        for (UNiagaraScript* Script : Scripts)
        {
            if (StaticEnum<ENiagaraScriptUsage>()->GetNameStringByValue(static_cast<int64>(Script->GetUsage())) != UsageName) continue;
            const FNiagaraVMExecutableData& Executable = Script->GetVMExecutableData();
            FNiagaraVMExecutableByteCode Bytecode = Executable.ByteCode;
            if (Bytecode.IsCompressed() && !Bytecode.Uncompress()) return SerializeJSON(Result);
            Result->SetStringField(TEXT("script"), Script->GetPathName());
            Result->SetStringField(TEXT("usage"), UsageName);
            Result->SetStringField(TEXT("bytecode_base64"), FBase64::Encode(Bytecode.GetData()));
            TArray<uint8> Literals;
            Executable.BakeScriptLiterals(Literals);
            Result->SetStringField(TEXT("literals_base64"), FBase64::Encode(Literals));
            Result->SetNumberField(TEXT("temporary_registers"), Executable.NumTempRegisters);
            TArray<TSharedPtr<FJsonValue>> Functions;
            for (const FVMExternalFunctionBindingInfo& Function : Executable.CalledVMExternalFunctions)
            {
                FObject Entry = MakeShared<FJsonObject>();
                Entry->SetStringField(TEXT("Name"), Function.Name.ToString());
                Entry->SetStringField(TEXT("OwnerName"), Function.OwnerName.ToString());
                Entry->SetNumberField(TEXT("NumInputs"), Function.GetNumInputs());
                Entry->SetNumberField(TEXT("NumOutputs"), Function.GetNumOutputs());
                Functions.Add(MakeShared<FJsonValueObject>(Entry));
            }
            Result->SetArrayField(TEXT("external_functions"), Functions);
            TArray<TSharedPtr<FJsonValue>> Scopes;
            for (const FNiagaraStatScope& Scope : Executable.StatScopes)
            {
                FObject Entry = MakeShared<FJsonObject>();
                Entry->SetStringField(TEXT("FullName"), Scope.FullName.ToString());
                Entry->SetStringField(TEXT("FriendlyName"), Scope.FriendlyName.ToString());
                Scopes.Add(MakeShared<FJsonValueObject>(Entry));
            }
            Result->SetArrayField(TEXT("stat_scopes"), Scopes);
            return SerializeJSON(Result);
        }
    }
    return SerializeJSON(Result);
}

FString UNiagaraWeaponAuthoring::AuditWeaponSystem(UNiagaraSystem* System, bool bCompile, bool bIncludeTranslation)
{
    FObject Result = MakeShared<FJsonObject>();
    if (!System) { Result->SetStringField(TEXT("error"), TEXT("Missing system")); return SerializeJSON(Result); }
    System->EnsureFullyLoaded();
    if (bCompile) { System->RequestCompile(true); System->WaitForCompilationComplete(false, false); }
    Result->SetStringField(TEXT("asset"), System->GetPathName());
    Result->SetBoolField(TEXT("ready"), System->IsReadyToRun());
    Result->SetBoolField(TEXT("valid"), System->IsValid());
    Result->SetBoolField(TEXT("can_render"), FApp::CanEverRender());
    Result->SetBoolField(TEXT("outstanding_compilation"), System->HasOutstandingCompilationRequests());
    Result->SetBoolField(TEXT("system_attributes_match"), System->GetSystemSpawnScript()->GetVMExecutableData().Attributes
        == System->GetSystemUpdateScript()->GetVMExecutableData().Attributes);
    TArray<TSharedPtr<FJsonValue>> Emitters;
    for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
    {
        const FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
        FObject Emitter = MakeShared<FJsonObject>();
        Emitter->SetStringField(TEXT("name"), Handle.GetName().ToString());
        Emitter->SetBoolField(TEXT("enabled"), Handle.GetIsEnabled());
        Emitter->SetStringField(TEXT("sim_target"), StaticEnum<ENiagaraSimTarget>()->GetNameStringByValue(static_cast<int64>(Data->SimTarget)));
        Emitter->SetBoolField(TEXT("ready"), Data->IsReadyToRun());
        Emitter->SetBoolField(TEXT("local_space"), Data->bLocalSpace);
        Emitter->SetBoolField(TEXT("determinism"), Data->bDeterminism);
        Emitter->SetNumberField(TEXT("random_seed"), Data->RandomSeed);
        Emitter->SetBoolField(TEXT("persistent_ids"), Data->bRequiresPersistentIDs);
        Emitter->SetBoolField(TEXT("interpolated_spawn"), Data->UsesInterpolatedSpawning());
        Emitter->SetArrayField(TEXT("functions"), DescribeGraph(CastChecked<UNiagaraScriptSource>(Data->GraphSource)->NodeGraph));
        FObject ModuleOrder = MakeShared<FJsonObject>();
        TArray<UNiagaraNodeOutput*> Outputs;
        CastChecked<UNiagaraScriptSource>(Data->GraphSource)->NodeGraph->GetNodesOfClass(Outputs);
        for (UNiagaraNodeOutput* Output : Outputs)
        {
            TArray<UNiagaraNodeFunctionCall*> Modules;
            OrderedModules(*Output, Modules);
            TArray<TSharedPtr<FJsonValue>> Entries;
            for (const UNiagaraNodeFunctionCall* Module : Modules)
            {
                FObject Entry = MakeShared<FJsonObject>();
                Entry->SetStringField(TEXT("name"), Module->GetFunctionName());
                Entry->SetStringField(TEXT("script"), GetPathNameSafe(Module->FunctionScript));
                Entry->SetBoolField(TEXT("assignment"), Module->IsA<UNiagaraNodeAssignment>());
                Entries.Add(MakeShared<FJsonValueObject>(Entry));
            }
            ModuleOrder->SetArrayField(StaticEnum<ENiagaraScriptUsage>()->GetNameStringByValue(
                static_cast<int64>(Output->GetUsage())), Entries);
        }
        Emitter->SetObjectField(TEXT("module_order"), ModuleOrder);
        TArray<TSharedPtr<FJsonValue>> Renderers;
        for (UNiagaraRendererProperties* Renderer : Data->GetRenderers())
        {
            FObject Entry = MakeShared<FJsonObject>();
            Entry->SetStringField(TEXT("class"), Renderer->GetClass()->GetName());
            Entry->SetNumberField(TEXT("sort_order"), Renderer->SortOrderHint);
            Entry->SetStringField(TEXT("source_mode"), StaticEnum<ENiagaraRendererSourceDataMode>()->GetNameStringByValue(static_cast<int64>(Renderer->GetCurrentSourceMode())));
            FObject AttributeBindings = MakeShared<FJsonObject>();
            for (TFieldIterator<FStructProperty> Property(Renderer->GetClass()); Property; ++Property)
            {
                if (Property->Struct == FNiagaraVariableAttributeBinding::StaticStruct())
                {
                    const auto* Binding = Property->ContainerPtrToValuePtr<FNiagaraVariableAttributeBinding>(Renderer);
                    AttributeBindings->SetStringField(Property->GetName(), Binding->GetParamMapBindableVariable().GetName().ToString());
                }
            }
            Entry->SetObjectField(TEXT("attribute_bindings"), AttributeBindings);
            const FNiagaraRendererMaterialParameters* MaterialParameters = nullptr;
            if (UNiagaraMeshRendererProperties* MeshRenderer = Cast<UNiagaraMeshRendererProperties>(Renderer))
            {
                Entry->SetStringField(TEXT("facing_mode"), StaticEnum<ENiagaraMeshFacingMode>()->GetNameStringByValue(static_cast<int64>(MeshRenderer->FacingMode)));
                Entry->SetStringField(TEXT("color_binding"), MeshRenderer->ColorBinding.GetParamMapBindableVariable().GetName().ToString());
                Entry->SetNumberField(TEXT("mesh_count"), MeshRenderer->Meshes.Num());
                Entry->SetBoolField(TEXT("locked_axis_enable"), MeshRenderer->bLockedAxisEnable);
                Entry->SetArrayField(TEXT("locked_axis"), { MakeShared<FJsonValueNumber>(MeshRenderer->LockedAxis.X), MakeShared<FJsonValueNumber>(MeshRenderer->LockedAxis.Y), MakeShared<FJsonValueNumber>(MeshRenderer->LockedAxis.Z) });
                Entry->SetStringField(TEXT("scale_binding"), MeshRenderer->ScaleBinding.GetParamMapBindableVariable().GetName().ToString());
                if (MeshRenderer->Meshes.Num())
                {
                    const auto& MeshEntry = MeshRenderer->Meshes[0];
                    Entry->SetStringField(TEXT("mesh"), GetPathNameSafe(MeshEntry.Mesh));
                    Entry->SetArrayField(TEXT("pivot_offset"), { MakeShared<FJsonValueNumber>(MeshEntry.PivotOffset.X), MakeShared<FJsonValueNumber>(MeshEntry.PivotOffset.Y), MakeShared<FJsonValueNumber>(MeshEntry.PivotOffset.Z) });
                    Entry->SetStringField(TEXT("pivot_offset_space"), StaticEnum<ENiagaraMeshPivotOffsetSpace>()->GetNameStringByValue(static_cast<int64>(MeshEntry.PivotOffsetSpace)));
                }
                if (MeshRenderer->OverrideMaterials.Num()) Entry->SetStringField(TEXT("material"), GetPathNameSafe(MeshRenderer->OverrideMaterials[0].ExplicitMat));
                MaterialParameters = &MeshRenderer->MaterialParameters;
            }
            if (UNiagaraSpriteRendererProperties* SpriteRenderer = Cast<UNiagaraSpriteRendererProperties>(Renderer))
            {
                Entry->SetStringField(TEXT("color_binding"), SpriteRenderer->ColorBinding.GetParamMapBindableVariable().GetName().ToString());
                Entry->SetStringField(TEXT("material"), GetPathNameSafe(SpriteRenderer->Material));
                Entry->SetArrayField(TEXT("pivot_in_uv_space"), { MakeShared<FJsonValueNumber>(SpriteRenderer->PivotInUVSpace.X), MakeShared<FJsonValueNumber>(SpriteRenderer->PivotInUVSpace.Y) });
                Entry->SetStringField(TEXT("facing_mode"), StaticEnum<ENiagaraSpriteFacingMode>()->GetNameStringByValue(static_cast<int64>(SpriteRenderer->FacingMode)));
                Entry->SetArrayField(TEXT("sub_image_size"), { MakeShared<FJsonValueNumber>(SpriteRenderer->SubImageSize.X), MakeShared<FJsonValueNumber>(SpriteRenderer->SubImageSize.Y) });
                Entry->SetBoolField(TEXT("sub_image_blend"), SpriteRenderer->bSubImageBlend);
                MaterialParameters = &SpriteRenderer->MaterialParameters;
            }
            Entry->SetStringField(TEXT("motion_vector_setting"), StaticEnum<ENiagaraRendererMotionVectorSetting>()->GetNameStringByValue(static_cast<int64>(Renderer->MotionVectorSetting)));
            if (MaterialParameters)
            {
                TArray<TSharedPtr<FJsonValue>> Bindings;
                for (const FNiagaraMaterialAttributeBinding& Binding : MaterialParameters->AttributeBindings)
                {
                    FObject Value = MakeShared<FJsonObject>();
                    Value->SetStringField(TEXT("material_parameter"), Binding.MaterialParameterName.ToString());
                    Value->SetStringField(TEXT("variable"), Binding.NiagaraVariable.GetName().ToString());
                    Value->SetStringField(TEXT("resolved_variable"), Binding.GetParamMapBindableVariable().GetName().ToString());
                    Value->SetStringField(TEXT("type"), Binding.NiagaraVariable.GetType().GetName());
                    Bindings.Add(MakeShared<FJsonValueObject>(Value));
                }
                Entry->SetArrayField(TEXT("material_bindings"), Bindings);
            }
            Renderers.Add(MakeShared<FJsonValueObject>(Entry));
        }
        Emitter->SetArrayField(TEXT("renderers"), Renderers);
        TArray<UNiagaraScript*> Scripts;
        Data->GetScripts(Scripts, true);
        TArray<TSharedPtr<FJsonValue>> Statuses;
        for (UNiagaraScript* Script : Scripts)
        {
            FObject Status = MakeShared<FJsonObject>();
            Status->SetStringField(TEXT("usage"), StaticEnum<ENiagaraScriptUsage>()->GetNameStringByValue(static_cast<int64>(Script->GetUsage())));
            Status->SetStringField(TEXT("status"), StaticEnum<ENiagaraScriptCompileStatus>()->GetNameStringByValue(static_cast<int64>(Script->GetLastCompileStatus())));
            Status->SetStringField(TEXT("compile_error"), Script->GetVMExecutableData().ErrorMsg);
            Status->SetNumberField(TEXT("translation_length"), Script->GetVMExecutableData().LastHlslTranslation.Len());
            if (bIncludeTranslation)
            {
                Status->SetStringField(TEXT("hlsl_translation"), Script->GetVMExecutableData().LastHlslTranslation);
            }
            Status->SetStringField(TEXT("script"), Script->GetPathName());
            Status->SetBoolField(TEXT("compilable"), Script->IsCompilable());
            Status->SetBoolField(TEXT("source_synchronized"), Script->AreScriptAndSourceSynchronized());
            Status->SetBoolField(TEXT("compiled_id_valid"), Script->GetVMExecutableDataCompilationId().IsValid());
            Status->SetNumberField(TEXT("compile_event_count"), Script->GetVMExecutableData().LastCompileEvents.Num());
            Statuses.Add(MakeShared<FJsonValueObject>(Status));
        }
        Emitter->SetArrayField(TEXT("compile_status"), Statuses);
        Emitters.Add(MakeShared<FJsonValueObject>(Emitter));
    }
    Result->SetArrayField(TEXT("emitters"), Emitters);
    TArray<TSharedPtr<FJsonValue>> SystemStatuses;
    for (UNiagaraScript* Script : { System->GetSystemSpawnScript(), System->GetSystemUpdateScript() })
    {
        FObject Status = MakeShared<FJsonObject>();
        Status->SetStringField(TEXT("usage"), StaticEnum<ENiagaraScriptUsage>()->GetNameStringByValue(static_cast<int64>(Script->GetUsage())));
        Status->SetStringField(TEXT("status"), StaticEnum<ENiagaraScriptCompileStatus>()->GetNameStringByValue(static_cast<int64>(Script->GetLastCompileStatus())));
        SystemStatuses.Add(MakeShared<FJsonValueObject>(Status));
    }
    Result->SetArrayField(TEXT("system_compile_status"), SystemStatuses);
    UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(System->GetSystemSpawnScript()->GetLatestSource());
    Result->SetArrayField(TEXT("system_functions"), DescribeGraph(Source ? Source->NodeGraph : nullptr));
    return SerializeJSON(Result);
}
