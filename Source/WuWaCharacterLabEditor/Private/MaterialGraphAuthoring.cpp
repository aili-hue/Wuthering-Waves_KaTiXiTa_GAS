#include "MaterialGraphAuthoring.h"
#include "Dom/JsonObject.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionComment.h"
#include "Materials/MaterialExpressionNamedReroute.h"
#include "MaterialGraph/MaterialGraph.h"
#include "MaterialGraph/MaterialGraphSchema.h"
#include "GraphEditor.h"
#include "ImageUtils.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Slate/WidgetRenderer.h"
#include "RenderingThread.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Modules/ModuleManager.h"
#include "Materials/MaterialInstanceConstant.h"
#include "MaterialEditingLibrary.h"
#include "MaterialEditor/MaterialEditorInstanceConstant.h"
#include "MaterialEditor/DEditorParameterValue.h"
#include "IDetailsView.h"
#include "UObject/UObjectIterator.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"

namespace
{
bool IsShowcaseMaterial(const UMaterial* Material)
{
    return Material && Material->GetPathName().StartsWith(TEXT("/Game/WuWaShowcase/Materials/"));
}
}

TArray<FString> UMaterialGraphAuthoring::GetInputConnections(UMaterialExpression* Expression)
{
    TArray<FString> Result;
    if (!Expression) return Result;
    for (const FExpressionInput* Input : Expression->GetInputsView())
    {
        Result.Add(Input && Input->Expression ? FString::Printf(
            TEXT("(Expression=\"%s\",OutputIndex=%d,Mask=%d,MaskR=%d,MaskG=%d,MaskB=%d,MaskA=%d)"),
            *Input->Expression->GetPathName(), Input->OutputIndex, Input->Mask, Input->MaskR,
            Input->MaskG, Input->MaskB, Input->MaskA) : TEXT(""));
    }
    return Result;
}

UMaterialExpression* UMaterialGraphAuthoring::TraceInputSource(UMaterialExpression* Expression)
{
    TSet<UMaterialExpression*> Seen;
    while (Expression)
    {
        if (Seen.Contains(Expression)) return nullptr;
        Seen.Add(Expression);
        if (auto* Usage = Cast<UMaterialExpressionNamedRerouteUsage>(Expression)) Expression = Usage->Declaration;
        else if (auto* Declaration = Cast<UMaterialExpressionNamedRerouteDeclaration>(Expression)) Expression = Declaration->Input.Expression;
        else return Expression;
    }
    return nullptr;
}

UMaterial* UMaterialGraphAuthoring::CopyMaterialForParameterProbe(UMaterial* Material)
{
    if (!IsShowcaseMaterial(Material)) return nullptr;
    auto* Copy=DuplicateObject<UMaterial>(Material,GetTransientPackage());
    Copy->SetFlags(RF_Transient);
    return Copy;
}

UMaterialInstanceConstant* UMaterialGraphAuthoring::CopyInstanceForParameterProbe(
    UMaterialInstanceConstant* Instance, UMaterial* ProbeMaterial)
{
    if (!Instance || !Instance->GetPathName().StartsWith(TEXT("/Game/WuWaShowcase/")) ||
        !ProbeMaterial || ProbeMaterial->GetOutermost()!=GetTransientPackage()) return nullptr;
    auto* Copy=DuplicateObject<UMaterialInstanceConstant>(Instance,GetTransientPackage());
    Copy->SetFlags(RF_Transient);
    UMaterialEditingLibrary::SetMaterialInstanceParent(Copy,ProbeMaterial);
    UMaterialEditingLibrary::UpdateMaterialInstance(Copy);
    return Copy;
}

bool UMaterialGraphAuthoring::SetOriginalParameterValues(UMaterialInstanceConstant* Instance,
    const TMap<FName, float>& Scalars, const TMap<FName, FLinearColor>& Vectors,
    const TMap<FName, UTexture*>& Textures)
{
    if (!Instance || (!Instance->GetPathName().StartsWith(TEXT("/Game/WuWaShowcase/")) &&
        Instance->GetOutermost() != GetTransientPackage())) return false;
    Instance->Modify();
    for (const auto& Pair : Scalars) Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(Pair.Key), Pair.Value);
    for (const auto& Pair : Vectors) Instance->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(Pair.Key), Pair.Value);
    for (const auto& Pair : Textures) Instance->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(Pair.Key), Pair.Value);
    UMaterialEditingLibrary::UpdateMaterialInstance(Instance);
    return true;
}

FString UMaterialGraphAuthoring::InspectInstanceParameterEditor(UMaterialInstanceConstant* Instance, const FString& Filename)
{
    auto Result=MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("valid"),false);
    for (TObjectIterator<UMaterialEditorInstanceConstant> It; It; ++It)
    {
        if (It->SourceInstance!=Instance || !It->DetailsView.IsValid()) continue;
        TArray<TSharedPtr<FJsonValue>> Visible,Internal;
        for (const auto& Group : It->ParameterGroups)
            for (const auto& Parameter : Group.Parameters)
            {
                if (!Parameter) continue;
                const bool bVisible=It->VisibleExpressions.Contains(Parameter->ParameterInfo);
                auto Entry=MakeShared<FJsonObject>();
                Entry->SetStringField(TEXT("name"),Parameter->ParameterInfo.Name.ToString());
                Entry->SetStringField(TEXT("group"),Group.GroupName.ToString());
                (bVisible ? Visible : Internal).Add(MakeShared<FJsonValueObject>(Entry));
            }
        Result->SetBoolField(TEXT("valid"),true);
        Result->SetArrayField(TEXT("visible"),Visible);
        Result->SetArrayField(TEXT("hidden"),Internal);
        if (!Filename.IsEmpty())
        {
            const FString Allowed=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("Saved/WuWaAuthoring/Artifacts/MaterialParameters"));
            if (FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Filename),Allowed))
            {
                FWidgetRenderer Renderer(true,false);
                const FVector2D Size(1200,1200);
                const auto Details=It->DetailsView.Pin().ToSharedRef();
                UTextureRenderTarget2D* Target=Renderer.DrawWidget(Details,Size);
                Renderer.DrawWidget(Target,Details,Size,0.f);
                FlushRenderingCommands();
                TArray<FColor> Pixels;TArray<uint8> Compressed;
                if (Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels))
                {
                    FImageUtils::CompressImageArray(1200,1200,Pixels,Compressed);
                    Result->SetBoolField(TEXT("captured"),FFileHelper::SaveArrayToFile(Compressed,*Filename));
                }
            }
        }
        break;
    }
    FString Text;FJsonSerializer::Serialize(Result,TJsonWriterFactory<>::Create(&Text));return Text;
}

bool UMaterialGraphAuthoring::ExportGraphRegion(UMaterial* Material, const FString& Filename,
    float X, float Y, float Zoom, int32 Width, int32 Height)
{
    const FString Allowed = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() /
        TEXT("Saved/WuWaAuthoring/Artifacts/MaterialOrganization"));
    const FString Output = FPaths::ConvertRelativePathToFull(Filename);
    if (!IsShowcaseMaterial(Material) || !FPaths::IsUnderDirectory(Output, Allowed) ||
        IsRunningCommandlet() || !FSlateApplication::IsInitialized() ||
        Zoom <= 0.f || Zoom > 1.f || Width < 100 || Height < 100 || Width > 4096 || Height > 4096) return false;
    FModuleManager::Get().LoadModuleChecked<IModuleInterface>(TEXT("MaterialEditor"));
    auto* Graph = NewObject<UMaterialGraph>(GetTransientPackage());
    Graph->Material = Material;
    Graph->Schema = UMaterialGraphSchema::StaticClass();
    Graph->RebuildGraph();
    auto Editor = SNew(SGraphEditor).GraphToEdit(Graph).IsEditable(false).ShowGraphStateOverlay(false);
    auto Window = SNew(SWindow).ClientSize(FVector2D(Width, Height)).SupportsMaximize(false).SupportsMinimize(false)[Editor];
    FSlateApplication::Get().AddWindow(Window, true);
    Editor->NotifyGraphChanged();
    for (int32 Frame = 0; Frame < 3; ++Frame) FSlateApplication::Get().Tick();
    Editor->SetViewLocation(FVector2f(X, Y), Zoom);
    FWidgetRenderer Renderer(true, false);
    UTextureRenderTarget2D* Target = Renderer.DrawWidget(Editor, FVector2D(Width, Height));
    if (!Target) return false;
    Renderer.DrawWidget(Target, Editor, FVector2D(Width, Height), 0.f);
    FlushRenderingCommands();
    TArray<FColor> Pixels;
    if (!Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels) || Pixels.Num() != Width * Height) return false;
    TArray<uint8> Compressed;
    FImageUtils::CompressImageArray(Width, Height, Pixels, Compressed);
    FSlateApplication::Get().RequestDestroyWindow(Window);
    return FFileHelper::SaveArrayToFile(Compressed, *Output);
}

FString UMaterialGraphAuthoring::AuditPersistentProperties(UObject* Object)
{
    auto Properties = MakeShared<FJsonObject>();
    if (IsValid(Object))
    {
        for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
        {
            const FProperty* Property = *It;
            if (Property->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient |
                CPF_NonPIEDuplicateTransient | CPF_SkipSerialization)) continue;
            for (int32 Index = 0; Index < Property->ArrayDim; ++Index)
            {
                FString Value;
                Property->ExportTextItem_Direct(Value, Property->ContainerPtrToValuePtr<void>(Object, Index),
                    nullptr, Object, PPF_None);
                Value.ReplaceInline(*Object->GetPathName(), TEXT("@SELF@"), ESearchCase::CaseSensitive);
                const FString Key = Property->GetName() + (Property->ArrayDim > 1 ? FString::Printf(TEXT("[%d]"), Index) : TEXT(""));
                Properties->SetStringField(Key, Value);
            }
        }
    }
    FString Result;
    FJsonSerializer::Serialize(Properties, TJsonWriterFactory<>::Create(&Result));
    return Result;
}

bool UMaterialGraphAuthoring::LinkNamedReroute(UMaterialExpressionNamedRerouteUsage* Usage,
    UMaterialExpressionNamedRerouteDeclaration* Declaration)
{
    if (!Usage || !Declaration || Usage->GetOuter() != Declaration->GetOuter()) return false;
    auto* Material = Cast<UMaterial>(Usage->GetOuter());
    if (!IsShowcaseMaterial(Material) || !Material->GetExpressions().Contains(Usage) ||
        !Material->GetExpressions().Contains(Declaration) || !Declaration->VariableGuid.IsValid()) return false;
    Usage->Modify();
    Usage->Declaration = Declaration;
    Usage->DeclarationGuid = Declaration->VariableGuid;
    Material->MarkPackageDirty();
    return true;
}

UMaterialExpressionComment* UMaterialGraphAuthoring::CreateModuleComment(UMaterial* Material,
    const FString& Title, int32 X, int32 Y, int32 Width, int32 Height, FLinearColor Color)
{
    if (!IsShowcaseMaterial(Material) || Width <= 0 || Height <= 0) return nullptr;
    Material->Modify();
    auto* Comment = NewObject<UMaterialExpressionComment>(Material, NAME_None, RF_Transactional);
    Comment->Material = Material;
    Comment->Text = Title;
    Comment->MaterialExpressionEditorX = X;
    Comment->MaterialExpressionEditorY = Y;
    Comment->SizeX = Width;
    Comment->SizeY = Height;
    Comment->CommentColor = Color;
    Comment->FontSize = 24;
    Material->GetExpressionCollection().AddComment(Comment);
    Material->MarkPackageDirty();
    return Comment;
}

bool UMaterialGraphAuthoring::PositionOutput(UMaterial* Material, int32 X, int32 Y)
{
    if (!IsShowcaseMaterial(Material)) return false;
    Material->Modify();
    Material->EditorX = X;
    Material->EditorY = Y;
    Material->MarkPackageDirty();
    return true;
}
