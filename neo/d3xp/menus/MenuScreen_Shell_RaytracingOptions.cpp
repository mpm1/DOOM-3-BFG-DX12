#pragma hdrstop
#include "../../idLib/precompiled.h"
#include "../Game_local.h"

const static int NUM_RAYTRACING_OPTIONS = 3;

extern idCVar s_raysCastPerLight;
extern idCVar s_lightEmissiveRadius;
extern idCVar r_allLightsCastShadows;

namespace
{
	bool AdjustBoolOption(bool currentValue, int adjustment)
	{
		if (adjustment == 0)
		{
			return currentValue;
		}

		return !currentValue;
	}

	int AdjustOption(int currentValue, const int values[], int numValues, int adjustment) {
		int index = 0;
		for (int i = 0; i < numValues; i++) {
			if (currentValue == values[i]) {
				index = i;
				break;
			}
		}
		index += adjustment;
		while (index < 0) {
			index += numValues;
		}
		index %= numValues;
		return values[index];
	}

	float LinearAdjust(float input, float currentMin, float currentMax, float desiredMin, float desiredMax) {
		return ((input - currentMin) / (currentMax - currentMin)) * (desiredMax - desiredMin) + desiredMin;
	}
}

void idMenuScreen_Shell_RaytracingOptions::Initialize(idMenuHandler* data)
{
	idMenuScreen::Initialize(data);

	if (data != NULL) {
		menuGUI = data->GetGUI();
	}

	SetSpritePath("menuSystemOptions"); // Using system options path uintil we can create our own

	options = new (TAG_SWF) idMenuWidget_DynamicList();
	options->SetNumVisibleOptions(NUM_RAYTRACING_OPTIONS);
	options->SetSpritePath(GetSpritePath(), "info", "options");
	options->SetWrappingAllowed(true);
	options->SetControlList(true);
	options->Initialize(data);

	btnBack = new (TAG_SWF) idMenuWidget_Button();
	btnBack->Initialize(data);
	btnBack->SetLabel("#str_swf_settings");
	btnBack->SetSpritePath(GetSpritePath(), "info", "btnBack");
	btnBack->AddEventAction(WIDGET_EVENT_PRESS).Set(WIDGET_ACTION_GO_BACK);

	AddChild(options);
	AddChild(btnBack);

	idMenuWidget_ControlButton* control;

	control = new (TAG_SWF) idMenuWidget_ControlButton();
	control->SetOptionType(OPTION_SLIDER_TEXT);
	control->SetLabel("All Lights Cast Shadow"); // Force all lights to cast a shadow
	control->SetDataSource(&systemData, idMenuDataSource_RaytracingSettings::RAYTRACING_FIELD_ALL_LIGHTS);
	control->SetupEvents(DEFAULT_REPEAT_TIME, options->GetChildren().Num());
	control->AddEventAction(WIDGET_EVENT_PRESS).Set(WIDGET_ACTION_COMMAND, idMenuDataSource_RaytracingSettings::RAYTRACING_FIELD_ALL_LIGHTS);
	options->AddChild(control);

	control = new (TAG_SWF) idMenuWidget_ControlButton();
	control->SetOptionType(OPTION_SLIDER_BAR);
	control->SetLabel("Light Emissive Radius");	// Emmisive Radius
	control->SetDataSource(&systemData, idMenuDataSource_RaytracingSettings::RAYTRACING_FIELD_EMISSIVE_RAD);
	control->SetupEvents(2, options->GetChildren().Num());
	control->AddEventAction(WIDGET_EVENT_PRESS).Set(WIDGET_ACTION_COMMAND, idMenuDataSource_RaytracingSettings::RAYTRACING_FIELD_EMISSIVE_RAD);
	options->AddChild(control);

	control = new (TAG_SWF) idMenuWidget_ControlButton();
	control->SetOptionType(OPTION_SLIDER_TEXT);
	control->SetLabel("Rays Per Pixel");
	control->SetDataSource(&systemData, idMenuDataSource_RaytracingSettings::RAYTRACING_FIELD_RAYS_PER_LIGHT);
	control->SetupEvents(2, options->GetChildren().Num());
	control->AddEventAction(WIDGET_EVENT_PRESS).Set(WIDGET_ACTION_COMMAND, idMenuDataSource_RaytracingSettings::RAYTRACING_FIELD_RAYS_PER_LIGHT);
	options->AddChild(control);

	options->AddEventAction(WIDGET_EVENT_SCROLL_DOWN).Set(new (TAG_SWF) idWidgetActionHandler(options, WIDGET_ACTION_EVENT_SCROLL_DOWN_START_REPEATER, WIDGET_EVENT_SCROLL_DOWN));
	options->AddEventAction(WIDGET_EVENT_SCROLL_UP).Set(new (TAG_SWF) idWidgetActionHandler(options, WIDGET_ACTION_EVENT_SCROLL_UP_START_REPEATER, WIDGET_EVENT_SCROLL_UP));
	options->AddEventAction(WIDGET_EVENT_SCROLL_DOWN_RELEASE).Set(new (TAG_SWF) idWidgetActionHandler(options, WIDGET_ACTION_EVENT_STOP_REPEATER, WIDGET_EVENT_SCROLL_DOWN_RELEASE));
	options->AddEventAction(WIDGET_EVENT_SCROLL_UP_RELEASE).Set(new (TAG_SWF) idWidgetActionHandler(options, WIDGET_ACTION_EVENT_STOP_REPEATER, WIDGET_EVENT_SCROLL_UP_RELEASE));
	options->AddEventAction(WIDGET_EVENT_SCROLL_DOWN_LSTICK).Set(new (TAG_SWF) idWidgetActionHandler(options, WIDGET_ACTION_EVENT_SCROLL_DOWN_START_REPEATER, WIDGET_EVENT_SCROLL_DOWN_LSTICK));
	options->AddEventAction(WIDGET_EVENT_SCROLL_UP_LSTICK).Set(new (TAG_SWF) idWidgetActionHandler(options, WIDGET_ACTION_EVENT_SCROLL_UP_START_REPEATER, WIDGET_EVENT_SCROLL_UP_LSTICK));
	options->AddEventAction(WIDGET_EVENT_SCROLL_DOWN_LSTICK_RELEASE).Set(new (TAG_SWF) idWidgetActionHandler(options, WIDGET_ACTION_EVENT_STOP_REPEATER, WIDGET_EVENT_SCROLL_DOWN_LSTICK_RELEASE));
	options->AddEventAction(WIDGET_EVENT_SCROLL_UP_LSTICK_RELEASE).Set(new (TAG_SWF) idWidgetActionHandler(options, WIDGET_ACTION_EVENT_STOP_REPEATER, WIDGET_EVENT_SCROLL_UP_LSTICK_RELEASE));
}

void idMenuScreen_Shell_RaytracingOptions::Update()
{
	if (menuData != NULL) {
		idMenuWidget_CommandBar* cmdBar = menuData->GetCmdBar();
		if (cmdBar != NULL) {
			cmdBar->ClearAllButtons();
			idMenuWidget_CommandBar::buttonInfo_t* buttonInfo;
			buttonInfo = cmdBar->GetButton(idMenuWidget_CommandBar::BUTTON_JOY2);
			if (menuData->GetPlatform() != 2) {
				buttonInfo->label = "#str_00395";
			}
			buttonInfo->action.Set(WIDGET_ACTION_GO_BACK);

			buttonInfo = cmdBar->GetButton(idMenuWidget_CommandBar::BUTTON_JOY1);
			buttonInfo->action.Set(WIDGET_ACTION_PRESS_FOCUSED);
		}
	}

	idSWFScriptObject& root = GetSWFObject()->GetRootObject();
	if (BindSprite(root)) {
		idSWFTextInstance* heading = GetSprite()->GetScriptObject()->GetNestedText("info", "txtHeading");
		if (heading != NULL) {
			heading->SetText("Raytracing");	// FULLSCREEN
			heading->SetText("Raytracing");	// FULLSCREEN
			heading->SetStrokeInfo(true, 0.75f, 1.75f);
		}

		idSWFSpriteInstance* gradient = GetSprite()->GetScriptObject()->GetNestedSprite("info", "gradient");
		if (gradient != NULL && heading != NULL) {
			gradient->SetXPos(heading->GetTextLength());
		}
	}

	if (btnBack != NULL) {
		btnBack->BindSprite(root);
	}

	idMenuScreen::Update();
}

void idMenuScreen_Shell_RaytracingOptions::ShowScreen(const mainMenuTransition_t transitionType)
{
	systemData.LoadData();

	idMenuScreen::ShowScreen(transitionType);
}

void idMenuScreen_Shell_RaytracingOptions::HideScreen(const mainMenuTransition_t transitionType) 
{
	if (systemData.IsDataChanged()) {
		systemData.CommitData();
	}
	idMenuScreen::HideScreen(transitionType);
}

bool idMenuScreen_Shell_RaytracingOptions::HandleAction(idWidgetAction& action, const idWidgetEvent& event, idMenuWidget* widget, bool forceHandled)
{

	if (menuData == NULL) {
		return true;
	}

	if (menuData->ActiveScreen() != SHELL_AREA_RAYTRACING_OPTIONS) {
		return false;
	}

	widgetAction_t actionType = action.GetType();
	const idSWFParmList& parms = action.GetParms();

	switch (actionType) {
	case WIDGET_ACTION_GO_BACK: {
		if (menuData != NULL) {
			menuData->SetNextScreen(SHELL_AREA_SETTINGS, MENU_TRANSITION_SIMPLE);
		}
		return true;
	}
	case WIDGET_ACTION_ADJUST_FIELD:
		break;
	case WIDGET_ACTION_COMMAND: {

		if (options == NULL) {
			return true;
		}

		int selectionIndex = options->GetFocusIndex();
		if (parms.Num() > 0) {
			selectionIndex = parms[0].ToInteger();
		}

		if (options && selectionIndex != options->GetFocusIndex()) {
			options->SetViewIndex(options->GetViewOffset() + selectionIndex);
			options->SetFocusIndex(selectionIndex);
		}

		/*switch (parms[0].ToInteger()) {
		default: {*/
			systemData.AdjustField(parms[0].ToInteger(), 1);
			options->Update();
		/*}
		}*/

		return true;
	}
	case WIDGET_ACTION_START_REPEATER: {

		if (options == NULL) {
			return true;
		}

		if (parms.Num() == 4) {
			int selectionIndex = parms[3].ToInteger();
			if (selectionIndex != options->GetFocusIndex()) {
				options->SetViewIndex(options->GetViewOffset() + selectionIndex);
				options->SetFocusIndex(selectionIndex);
			}
		}
		break;
	}
	}

	return idMenuWidget::HandleAction(action, event, widget, forceHandled);
}

idMenuScreen_Shell_RaytracingOptions::idMenuDataSource_RaytracingSettings::idMenuDataSource_RaytracingSettings()
{

}

void idMenuScreen_Shell_RaytracingOptions::idMenuDataSource_RaytracingSettings::LoadData()
{
	originalUseAllLights = r_allLightsCastShadows.GetBool();
	originalEmissiveRadius = s_lightEmissiveRadius.GetFloat();
	originalRaysPerPixek = s_raysCastPerLight.GetInteger();
}

bool idMenuScreen_Shell_RaytracingOptions::idMenuDataSource_RaytracingSettings::IsRestartRequired() const
{
	// TODO: We may need to do this for enabling raytracing later on.
	return false;
}

void idMenuScreen_Shell_RaytracingOptions::idMenuDataSource_RaytracingSettings::CommitData() {
	cvarSystem->SetModifiedFlags(CVAR_ARCHIVE);
}

void idMenuScreen_Shell_RaytracingOptions::idMenuDataSource_RaytracingSettings::AdjustField(const int fieldIndex, const int adjustAmount) {
	switch (fieldIndex) {
	case RAYTRACING_FIELD_ALL_LIGHTS: {
		r_allLightsCastShadows.SetBool(AdjustBoolOption(r_allLightsCastShadows.GetBool(), adjustAmount));
		break;
	}
	case RAYTRACING_FIELD_RAYS_PER_LIGHT: {
		static const int numValues = 10;
		static const int values[numValues] = { 1, 3, 5, 10, 20, 30, 100, 200, 500, 1000 };
		s_raysCastPerLight.SetInteger(AdjustOption(s_raysCastPerLight.GetInteger(), values, numValues, adjustAmount));
		break;
	}
	case RAYTRACING_FIELD_EMISSIVE_RAD: {
		const float percent = LinearAdjust(s_lightEmissiveRadius.GetFloat(), 0.0f, 60.0f, 0.0f, 100.0f);
		const float adjusted = percent + (float)adjustAmount;
		const float clamped = idMath::ClampFloat(0.0f, 100.0f, adjusted);
		s_lightEmissiveRadius.SetFloat(LinearAdjust(clamped, 0.0f, 100.0f, 0.0f, 60.0f));
		break;
	}
	}

	cvarSystem->ClearModifiedFlags(CVAR_ARCHIVE);
}

idSWFScriptVar idMenuScreen_Shell_RaytracingOptions::idMenuDataSource_RaytracingSettings::GetField(const int fieldIndex) const {
	switch (fieldIndex) {
	case RAYTRACING_FIELD_ALL_LIGHTS: {
		const bool useAllLights = r_allLightsCastShadows.GetBool();
		if (useAllLights)
		{
			return "#str_swf_enabled";
		}
		else
		{
			return "#str_swf_disabled";
		}
	}
	case RAYTRACING_FIELD_RAYS_PER_LIGHT:
		return va("%d", s_raysCastPerLight.GetInteger());
	case RAYTRACING_FIELD_EMISSIVE_RAD:
		return LinearAdjust(s_lightEmissiveRadius.GetFloat(), 0.0f, 60.0f, 0.0f, 100.0f);
	}

	return false;
}

bool idMenuScreen_Shell_RaytracingOptions::idMenuDataSource_RaytracingSettings::IsDataChanged() const
{
	if (originalUseAllLights != r_allLightsCastShadows.GetBool())
	{ 
		return true;
	}
	if (originalEmissiveRadius != s_lightEmissiveRadius.GetFloat())
	{
		return true;
	}
	if (originalRaysPerPixek != static_cast<UINT>(s_raysCastPerLight.GetInteger()))
	{
		return true;
	}

	return false;
}

