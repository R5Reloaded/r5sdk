#pragma once
#include "core/stdafx.h"

//LOBYTE is already defined from pseudodefs, however this definition is not the same as the windows one
#pragma push_macro("LOBYTE")
#undef LOBYTE
#define LOBYTE(w) ((BYTE)(((DWORD_PTR)(w)) & 0xff))
#include <gdiplus.h>
#pragma pop_macro("LOBYTE")

#include "thirdparty/cppnet/cppkore/Form.h"
#include "thirdparty/cppnet/cppkore/Kore.h"
#include "thirdparty/cppnet/cppkore/UIXTheme.h"
#include "thirdparty/cppnet/cppkore/UIXLabel.h"
#include "thirdparty/cppnet/cppkore/UIXListView.h"
#include "thirdparty/cppnet/cppkore/UIXCheckBox.h"
#include "thirdparty/cppnet/cppkore/UIXComboBox.h"
#include "thirdparty/cppnet/cppkore/UIXTextBox.h"
#include "thirdparty/cppnet/cppkore/UIXGroupBox.h"
#include "thirdparty/cppnet/cppkore/UIXButton.h"
#include "thirdparty/cppnet/cppkore/UIXRadioButton.h"
#include "thirdparty/cppnet/cppkore/KoreTheme.h"

#include "sdklauncher/sdklauncher_const.h"
