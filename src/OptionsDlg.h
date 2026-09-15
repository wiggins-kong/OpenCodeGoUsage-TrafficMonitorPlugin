// OptionsDlg.h —— 插件选项对话框（原生 Win32，不依赖 MFC）
#pragma once

#include "TmSdk.h"
#include "TmCommon.h"

namespace ocg
{
    // 由 ITMPlugin::ShowOptionsDialog 调用；返回值直接回给宿主
    ITMPlugin::OptionReturn ShowPluginOptions(HWND hParent);
}
