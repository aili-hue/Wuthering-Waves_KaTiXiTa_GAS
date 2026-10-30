#include "MaterialParameterEditor.h"
#include "Containers/Ticker.h"
#include "MaterialEditorModule.h"
#include "IMaterialEditor.h"
#include "MaterialEditor/MaterialEditorInstanceConstant.h"
#include "MaterialEditor/DEditorParameterValue.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/Material.h"
#include "IDetailsView.h"
#include "PropertyEditorDelegates.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Modules/ModuleManager.h"
#include "UObject/UObjectIterator.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"

struct FWuWaMaterialParameterEditorState
{
    TWeakPtr<IMaterialEditor> Editor;
    TWeakObjectPtr<UMaterialEditorInstanceConstant> Model;
    TWeakPtr<IDetailsView> Details;
    FIsPropertyVisible PreviousVisible;
    TSharedPtr<FExtender> Toolbar;
    FDelegateHandle Changed;
    FTSTicker::FDelegateHandle PendingRefresh;
    TSet<FMaterialParameterInfo> Internal;
    bool bShowInternal = false;
};

namespace
{
void Refresh(const TSharedPtr<FWuWaMaterialParameterEditorState>& State)
{
    auto* Model = State->Model.Get();
    if (!Model || !Model->SourceInstance) return;
    TArray<FString> SourceNames;
    Model->SourceInstance->GetOutermost()->GetMetaData().GetValue(
        Model->SourceInstance, TEXT("WuWaOriginalParameterNames")).ParseIntoArray(SourceNames, TEXT("\n"), true);
    TSet<FName> Allowed;
    for (const FString& Name : SourceNames) Allowed.Add(FName(*Name));
    State->Internal.Reset();
    for (const auto& Group : Model->ParameterGroups)
        for (const auto& Parameter : Group.Parameters)
            if (Parameter && (Group.GroupName.ToString().StartsWith(TEXT("9")) ||
                (!Allowed.IsEmpty() && !Allowed.Contains(Parameter->ParameterInfo.Name))))
                State->Internal.Add(Parameter->ParameterInfo);
    FModuleManager::GetModuleChecked<IMaterialEditorModule>(TEXT("MaterialEditor")).GetVisibleMaterialParameters(
        Model->SourceInstance->GetMaterial(), Model->SourceInstance, Model->VisibleExpressions);
    if (!State->bShowInternal)
        Model->VisibleExpressions.RemoveAll([&](const FMaterialParameterInfo& Info){ return State->Internal.Contains(Info); });
    if (auto Details = State->Details.Pin()) Details->ForceRefresh();
}
}

void FWuWaMaterialParameterEditor::Startup()
{
    auto& Module = FModuleManager::LoadModuleChecked<IMaterialEditorModule>(TEXT("MaterialEditor"));
    OpenedHandle = Module.OnMaterialInstanceEditorOpened().AddLambda([this](TWeakPtr<IMaterialEditor> Editor)
    {
        Pending.Add(FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
            [this, Editor](float){ OnOpened(Editor); return false; })));
    });
}

void FWuWaMaterialParameterEditor::OnOpened(TWeakPtr<IMaterialEditor> WeakEditor)
{
    auto Editor = WeakEditor.Pin();
    if (!Editor) return;
    UMaterialEditorInstanceConstant* Model = nullptr;
    for (TObjectIterator<UMaterialEditorInstanceConstant> It; It; ++It)
        if (It->SourceInstance == Editor->GetMaterialInterface() || It->PreviewInstance == Editor->GetMaterialInterface())
        { Model = *It; break; }
    if (!Model || !Model->SourceInstance ||
        !Model->SourceInstance->GetPathName().StartsWith(TEXT("/Game/WuWaShowcase/"))) return;
    States.RemoveAll([](const auto& State){ return !State->Editor.IsValid(); });
    auto State = MakeShared<FWuWaMaterialParameterEditorState>();
    State->Editor = Editor; State->Model = Model; State->Details = Model->DetailsView;
    if (auto Details = State->Details.Pin())
    {
        State->PreviousVisible = Details->GetIsPropertyVisibleDelegate();
        Details->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateLambda([State](const FPropertyAndParent& Property)
        {
            if (State->PreviousVisible.IsBound() && !State->PreviousVisible.Execute(Property)) return false;
            if (!State->bShowInternal)
                for (auto Object : Property.Objects)
                    if (auto* Parameter = Cast<UDEditorParameterValue>(Object.Get()))
                        if (State->Internal.Contains(Parameter->ParameterInfo)) return false;
            return true;
        }));
        State->Changed = Details->OnFinishedChangingProperties().AddLambda(
            [WeakState=TWeakPtr<FWuWaMaterialParameterEditorState>(State)](const FPropertyChangedEvent&)
            {
                if (auto Current=WeakState.Pin())
                {
                    FTSTicker::RemoveTicker(Current->PendingRefresh);
                    Current->PendingRefresh=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
                        [WeakState](float){ if (auto Ready=WeakState.Pin()) Refresh(Ready); return false; }));
                }
            });
        Refresh(State);
    }
    State->Toolbar=MakeShared<FExtender>();
    State->Toolbar->AddToolBarExtension(TEXT("Asset"),EExtensionHook::After,Editor->GetToolkitCommands(),
        FToolBarExtensionDelegate::CreateLambda([WeakState=TWeakPtr<FWuWaMaterialParameterEditorState>(State)](FToolBarBuilder& Builder)
        {
            auto State=WeakState.Pin();
            if (!State) return;
            Builder.AddToolBarButton(FUIAction(FExecuteAction::CreateLambda([State]
                { State->bShowInternal=!State->bShowInternal; Refresh(State); }),FCanExecuteAction(),
                FIsActionChecked::CreateLambda([State]{return State->bShowInternal;})),NAME_None,
                NSLOCTEXT("WuWaMaterial","InternalData","显示内部数据"),
                NSLOCTEXT("WuWaMaterial","InternalDataHelp","查看编译常量、渲染分支和程序驱动输入。常规调材质使用原版参数名称。"),
                FSlateIcon(),EUserInterfaceActionType::ToggleButton);
        }));
    Editor->AddToolbarExtender(State->Toolbar);
    Editor->RegenerateMenusAndToolbars();
    States.Add(State);
}

void FWuWaMaterialParameterEditor::Shutdown()
{
    for (auto Handle : Pending) FTSTicker::RemoveTicker(Handle);
    Pending.Reset();
    if (auto* Module=FModuleManager::GetModulePtr<IMaterialEditorModule>(TEXT("MaterialEditor")))
        Module->OnMaterialInstanceEditorOpened().Remove(OpenedHandle);
    for (const auto& State : States)
    {
        FTSTicker::RemoveTicker(State->PendingRefresh);
        if (auto Details=State->Details.Pin())
        {
            Details->OnFinishedChangingProperties().Remove(State->Changed);
            Details->SetIsPropertyVisibleDelegate(State->PreviousVisible);
        }
    }
    States.Reset();
}
