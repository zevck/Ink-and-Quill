Scriptname InkAndQuill_MCM extends SKI_ConfigBase
{Ink & Quill's settings (docs/SETTINGS.md) and the mods using it.  Every value lives in the DLL's INI; a setting is
named "Section.Key", and its default and range come from the DLL too.}

int  Function GetSetting(string name)                  global native
     Function SetSetting(string name, int value)       global native
int  Function GetSettingDefault(string name)           global native
int  Function GetSettingMin(string name)               global native
int  Function GetSettingMax(string name)               global native
int    Function GetKeyProblem(int keyCode)             global native
int    Function GetClientCount()                       global native
string Function GetClientName(int index)               global native

int _editKeyOid
int _requireOid
int _usesOid
int _bloodOid
int _bloodCostOid

event OnConfigInit()
    ModName = "Ink & Quill"
endevent

; Greyed out when it doesn't apply: inkwells and blood without quill and ink required, the cost without blood.
int function WritingFlags()
    if GetSetting("Writing.RequireQuillAndInk") == 0
        return OPTION_FLAG_DISABLED
    endif
    return OPTION_FLAG_NONE
endfunction

int function BloodCostFlags()
    if GetSetting("Writing.Blood") == 0
        return OPTION_FLAG_DISABLED
    endif
    return WritingFlags()
endfunction

event OnPageReset(string page)
    SetCursorFillMode(TOP_TO_BOTTOM)
    AddHeaderOption("$IQ_HeaderKeys")
    _editKeyOid = AddKeyMapOption("$IQ_EditKey", GetSetting("Keys.Edit"))
    AddEmptyOption()
    AddHeaderOption("$IQ_HeaderWriting")
    _requireOid = AddToggleOption("$IQ_Materials", GetSetting("Writing.RequireQuillAndInk") != 0)
    _usesOid = AddSliderOption("$IQ_InkwellUses", GetSetting("Writing.InkwellUses"), "{0}", WritingFlags())
    _bloodOid = AddToggleOption("$IQ_Blood", GetSetting("Writing.Blood") != 0, WritingFlags())
    _bloodCostOid = AddSliderOption("$IQ_BloodCost", GetSetting("Writing.BloodCost"), "{0}%", BloodCostFlags())

    SetCursorPosition(1)
    AddHeaderOption("$IQ_HeaderClients")
    int count = GetClientCount()
    if count == 0
        AddTextOption("$IQ_NoClients", "", OPTION_FLAG_DISABLED)
    endif
    int i = 0
    while i < count
        AddTextOption(GetClientName(i), "")
        i += 1
    endwhile
endevent

; The setting behind a slider or toggle, or "".
string function SettingOf(int oid)
    if oid == _requireOid
        return "Writing.RequireQuillAndInk"
    elseif oid == _usesOid
        return "Writing.InkwellUses"
    elseif oid == _bloodOid
        return "Writing.Blood"
    elseif oid == _bloodCostOid
        return "Writing.BloodCost"
    elseif oid == _editKeyOid
        return "Keys.Edit"
    endif
    return ""
endfunction

; A slider's value format.
string function FormatOf(int oid)
    if oid == _bloodCostOid
        return "{0}%"
    endif
    return "{0}"
endfunction

function UpdateFlags()
    SetOptionFlags(_usesOid, WritingFlags(), true)
    SetOptionFlags(_bloodOid, WritingFlags(), true)
    SetOptionFlags(_bloodCostOid, BloodCostFlags())
endfunction

event OnOptionSelect(int oid)
    if oid == _requireOid || oid == _bloodOid
        string name = SettingOf(oid)
        bool enabled = GetSetting(name) == 0
        SetSetting(name, enabled as int)
        SetToggleOptionValue(oid, enabled)
        UpdateFlags()
    endif
endevent

event OnOptionSliderOpen(int oid)
    string name = SettingOf(oid)
    if oid == _usesOid || oid == _bloodCostOid
        SetSliderDialogStartValue(GetSetting(name))
        SetSliderDialogDefaultValue(GetSettingDefault(name))
        SetSliderDialogRange(GetSettingMin(name), GetSettingMax(name))
        SetSliderDialogInterval(1)
    endif
endevent

event OnOptionSliderAccept(int oid, float value)
    if oid == _usesOid || oid == _bloodCostOid
        string name = SettingOf(oid)
        SetSetting(name, value as int)
        SetSliderOptionValue(oid, GetSetting(name), FormatOf(oid))
    endif
endevent

; The key only acts while a book is open, where game controls don't apply: a conflict with one doesn't matter.  It
; must be a keyboard key that neither types nor edits (it would save instead).
event OnOptionKeyMapChange(int oid, int keyCode, string conflictControl, string conflictName)
    if oid != _editKeyOid
        return
    endif
    int problem = GetKeyProblem(keyCode)
    if problem == 1
        ShowMessage("$IQ_KeyNotKeyboard", false, "$IQ_Ok")
    elseif problem == 2
        ShowMessage("$IQ_KeyTypes", false, "$IQ_Ok")
    else
        SetSetting(SettingOf(oid), keyCode)
        SetKeyMapOptionValue(oid, GetSetting(SettingOf(oid)))
    endif
endevent

event OnOptionDefault(int oid)
    string name = SettingOf(oid)
    if name == ""
        return
    endif
    SetSetting(name, GetSettingDefault(name))
    int value = GetSetting(name)
    if oid == _editKeyOid
        SetKeyMapOptionValue(oid, value)
    elseif oid == _requireOid || oid == _bloodOid
        SetToggleOptionValue(oid, value != 0)
        UpdateFlags()
    else
        SetSliderOptionValue(oid, value, FormatOf(oid))
    endif
endevent

event OnOptionHighlight(int oid)
    if oid == _editKeyOid
        SetInfoText("$IQ_TipEditKey")
    elseif oid == _requireOid
        SetInfoText("$IQ_TipMaterials")
    elseif oid == _usesOid
        SetInfoText("$IQ_TipInkwellUses")
    elseif oid == _bloodOid
        SetInfoText("$IQ_TipBlood")
    elseif oid == _bloodCostOid
        SetInfoText("$IQ_TipBloodCost")
    elseif oid > 0
        SetInfoText("$IQ_TipClients")
    endif
endevent
