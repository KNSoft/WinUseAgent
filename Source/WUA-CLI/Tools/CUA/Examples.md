# CUA 调用示例

下面按当前源码列出每个公开 CUA 命令的命令行和 JSON **字段摘录**。数值、句柄、窗口内容和 UIA runtime ID 均为示意值，尚未运行采集。省略重复的窗口、UIA 和输入上下文字段；JSON 内不使用省略号。

`00010234` 代表示例顶层窗口，`00020234` 代表其中的编辑框。执行时从 Inspect 复制实际窗口句柄和 runtime_id。示例彼此独立，窗口/控件状态取决于目标程序。Session 省略时选当前 Windows 会话；数字 `7` 表示示例 child session。OutFile 相对于调用方的工作目录。

`ok` 表示主 API 是否成功，应用效果仍需看返回的观察信息或指定的截图。输入/窗口操作默认附带新观察；未指定 OutFile 时不截图。所有 X/Y 均相对于对应截图左上角，窗口截图包含非客户区。

## Inspect

### 桌面并保存截图

```powershell
WUA-CLI.exe CUA Inspect OutFile=desktop.png
```

```json
{
  "ok": true,
  "result": {
    "geometry": {"width": 1600, "height": 900},
    "windows": [
      {
        "window_handle": "00010234",
        "pid": 4200,
        "tid": 4204,
        "title": "CUA Demo",
        "foreground": true,
        "enabled": true,
        "minimized": false,
        "maximized": false,
        "bounds_image_px": {"left": 100, "top": 100, "right": 900, "bottom": 700}
      }
    ],
    "foreground": "00010234",
    "uia_target": "00010234",
    "uia_state": "ok",
    "elements": [
      {"name": "CUA Demo", "role": "窗口", "runtime_id": "42,66000,1"},
      {
        "name": "正文",
        "role": "编辑",
        "runtime_id": "42,66000,2",
        "parent": 0,
        "text": "Hello",
        "bounds_image_px": {"left": 120, "top": 150, "right": 880, "bottom": 650},
        "point_image_px": {"x": 500, "y": 400}
      }
    ],
    "interaction": {
      "state": "current",
      "focus": {"window_handle": "00020234", "within_observed_window": true, "control_class": "Edit"}
    }
  }
}
```

### 窗口并保存截图

同一编辑框在桌面观察和窗口观察中的坐标不同；按本次 Handle 对应的截图使用，转换由工具完成。

```powershell
WUA-CLI.exe CUA Inspect Handle=00010234 OutFile=window.png
```

```json
{
  "ok": true,
  "result": {
    "geometry": {"width": 800, "height": 600},
    "windows": [{"window_handle": "00010234", "title": "CUA Demo", "foreground": true}],
    "uia_target": "00010234",
    "uia_state": "ok",
    "elements": [
      {"name": "CUA Demo", "runtime_id": "42,66000,1"},
      {
        "name": "正文",
        "runtime_id": "42,66000,2",
        "parent": 0,
        "text": "Hello",
        "focused": true,
        "bounds_image_px": {"left": 20, "top": 50, "right": 780, "bottom": 550},
        "point_image_px": {"x": 400, "y": 300},
        "patterns": {"invoke": false, "set_value": true, "toggle": false, "select": false}
      }
    ],
    "interaction": {
      "state": "current",
      "focus": {"window_handle": "00020234", "within_observed_window": true}
    }
  }
}
```

### 只取信息，不截图

仍返回几何、窗口、UIA 和输入上下文；没有 PNG 产出。

```powershell
WUA-CLI.exe CUA Inspect Handle=00010234
```

```json
{
  "ok": true,
  "result": {
    "geometry": {"width": 800, "height": 600},
    "uia_state": "ok",
    "elements": [{"name": "正文", "runtime_id": "42,66000,2", "text": "Hello", "point_image_px": {"x": 400, "y": 300}}],
    "interaction": {"state": "current"}
  }
}
```

## Window

### Activate

```powershell
WUA-CLI.exe CUA Window Action=Activate Handle=00010234
```

```json
{
  "ok": true,
  "result": {
    "foreground": "00010234",
    "windows": [{"window_handle": "00010234", "foreground": true, "minimized": false}],
    "interaction": {"state": "current"}
  }
}
```

### Minimize

最小化后返回桌面观察。

```powershell
WUA-CLI.exe CUA Window Action=Minimize Handle=00010234
```

```json
{
  "ok": true,
  "result": {
    "geometry": {"width": 1600, "height": 900},
    "windows": [{"window_handle": "00010234", "minimized": true}],
    "interaction": {"state": "current"}
  }
}
```

### Maximize

```powershell
WUA-CLI.exe CUA Window Action=Maximize Handle=00010234
```

```json
{
  "ok": true,
  "result": {
    "windows": [{"window_handle": "00010234", "foreground": true, "maximized": true}],
    "interaction": {"state": "current"}
  }
}
```

### Close

返回桌面观察。成功表示提交了关闭请求；目标程序仍可能显示保存确认。

```powershell
WUA-CLI.exe CUA Window Action=Close Handle=00010234
```

```json
{
  "ok": true,
  "result": {"geometry": {"width": 1600, "height": 900}, "interaction": {"state": "current"}}
}
```

## Mouse

### Click

使用窗口 Inspect 给出的 point_image_px；省略 Handle 或使用 Handle=0 时使用桌面截图坐标。

```powershell
WUA-CLI.exe CUA Mouse Operation=Click Handle=00010234 X=400 Y=300
```

```json
{
  "ok": true,
  "result": {
    "interaction": {
      "state": "current",
      "focus": {"window_handle": "00020234", "within_observed_window": true}
    }
  }
}
```

### DoubleClick

```powershell
WUA-CLI.exe CUA Mouse Operation=DoubleClick Handle=00010234 X=400 Y=300
```

```json
{
  "ok": true,
  "result": {
    "interaction": {
      "state": "current",
      "focus": {"window_handle": "00020234", "within_observed_window": true}
    }
  }
}
```

### Move

```powershell
WUA-CLI.exe CUA Mouse Operation=Move Handle=00010234 X=400 Y=300
```

```json
{"ok": true, "result": {"interaction": {"state": "current"}}}
```

### Wheel

负的垂直 Delta 向下滚动；Axis=Horizontal 时正值向右。

```powershell
WUA-CLI.exe CUA Mouse Operation=Wheel Handle=00010234 X=400 Y=300 Delta=-120 Axis=Vertical
```

```json
{"ok": true, "result": {"interaction": {"state": "current"}}}
```

### Drag

完成按下、移动和松开；终点沿用同一截图坐标系。

```powershell
WUA-CLI.exe CUA Mouse Operation=Drag Handle=00010234 X=200 Y=200 ToX=500 ToY=400 DurationMs=300
```

```json
{
  "ok": true,
  "result": {"interaction": {"state": "current", "mouse_capture_window": "00000000"}}
}
```

### Down

此示例控件在按下后捕获鼠标；实际是否捕获取决于控件。完整手势可直接用 Drag。

```powershell
WUA-CLI.exe CUA Mouse Operation=Down Handle=00010234 X=400 Y=300 Button=Left
```

```json
{
  "ok": true,
  "result": {"interaction": {"state": "current", "mouse_capture_window": "00020234"}}
}
```

### Up

```powershell
WUA-CLI.exe CUA Mouse Operation=Up Handle=00010234 X=400 Y=300 Button=Left
```

```json
{
  "ok": true,
  "result": {"interaction": {"state": "current", "mouse_capture_window": "00000000"}}
}
```

## Text 和 Keys

### Text：默认 Message

先点击编辑框。Message 不动剪贴板；Paste 使用 Ctrl+V，并尽力恢复原 OLE 对象。

```powershell
WUA-CLI.exe CUA Text Handle=00010234 "Text=Hello 世界" OutFile=typed.png
```

```json
{
  "ok": true,
  "result": {
    "elements": [{"name": "正文", "text": "Hello 世界", "focused": true}],
    "interaction": {
      "state": "current",
      "focus": {"window_handle": "00020234", "within_observed_window": true}
    }
  }
}
```

### Text：Paste

```powershell
WUA-CLI.exe CUA Text Handle=00010234 Method=Paste "Text=Hello 世界" OutFile=pasted.png
```

```json
{
  "ok": true,
  "result": {
    "elements": [{"name": "正文", "text": "Hello 世界", "focused": true}],
    "interaction": {
      "state": "current",
      "focus": {"window_handle": "00020234", "within_observed_window": true}
    }
  }
}
```

### Keys

Keys 键名不区分大小写，ALT+F4、alt+f4 和 Alt+f4 等价。组合键 API 成功不等于应用执行了指定功能。示例选中全文，正文内容保持不变。

```powershell
WUA-CLI.exe CUA Keys Handle=00010234 Keys=CTRL+A
```

```json
{
  "ok": true,
  "result": {
    "elements": [{"name": "正文", "text": "Hello 世界", "focused": true}],
    "interaction": {
      "state": "current",
      "focus": {"window_handle": "00020234", "within_observed_window": true}
    }
  }
}
```

## Element

从 Inspect 复制元素的 runtime_id，使用同一窗口 Handle；按 patterns 选择控件支持的操作。RuntimeId 是原生有符号整数的逗号分隔字符串。

### Invoke

示例按钮的 Invoke 动作增加程序计数。

```powershell
WUA-CLI.exe CUA Element Operation=Invoke Handle=00010234 "RuntimeId=42,66000,3"
```

```json
{
  "ok": true,
  "result": {"elements": [{"name": "计数：1", "text": "1"}], "interaction": {"state": "current"}}
}
```

### SetValue

```powershell
WUA-CLI.exe CUA Element Operation=SetValue Handle=00010234 "RuntimeId=42,66000,2" "Text=Hello 世界"
```

```json
{
  "ok": true,
  "result": {"elements": [{"name": "正文", "runtime_id": "42,66000,2", "text": "Hello 世界", "patterns": {"set_value": true}}]}
}
```

### Toggle

通过请求的截图确认切换后的视觉状态。

```powershell
WUA-CLI.exe CUA Element Operation=Toggle Handle=00010234 "RuntimeId=42,66000,4" OutFile=toggled.png
```

```json
{"ok": true, "result": {"elements": [{"name": "启用选项", "runtime_id": "42,66000,4", "patterns": {"toggle": true}}]}}
```

### Select

```powershell
WUA-CLI.exe CUA Element Operation=Select Handle=00010234 "RuntimeId=42,66000,5" OutFile=selected.png
```

```json
{"ok": true, "result": {"elements": [{"name": "项目 A", "runtime_id": "42,66000,5", "patterns": {"select": true}}]}}
```

## Session

### CreateChild

等待登录、桌面及 Worker 就绪后才成功，返回可直接使用的 Windows Session ID。系统弹出凭据窗口时，指引用户在系统窗口输入密码；CLI 不接收密码。已有同分辨率 child 会被复用，内部有限恢复保留登录。

```powershell
WUA-CLI.exe CUA Session CreateChild Width=1600 Height=900 Show=false
```

```json
{"ok": true, "result": {"id": 7}}
```

### List

```powershell
WUA-CLI.exe CUA Session List
```

```json
{
  "ok": true,
  "result": [
    {"id": 3, "state": "ready", "owned": false},
    {
      "id": 7,
      "state": "ready",
      "owned": true,
      "preview_visible": false,
      "width": 1600,
      "height": 900
    }
  ]
}
```

### 在 child session 中操作

Window、Mouse、Text、Keys、Element 同样接受 Session=7；窗口句柄要从该会话的 Inspect 取得。

```powershell
WUA-CLI.exe CUA Inspect Session=7 OutFile=child.png
```

```json
{
  "ok": true,
  "result": {
    "geometry": {"width": 1600, "height": 900},
    "windows": [{"window_handle": "00010234", "title": "CUA Demo"}],
    "uia_state": "ok",
    "interaction": {"state": "current"}
  }
}
```

### Preview：显示

预览可见性不影响登录和 Worker。成功没有额外结果；需要会话诊断时使用 List。

```powershell
WUA-CLI.exe CUA Session Preview Session=7 Show=true
```

```json
{"ok": true, "result": null}
```

### Preview：隐藏

预览可见性不影响登录和 Worker。成功没有额外结果；需要会话诊断时使用 List。

```powershell
WUA-CLI.exe CUA Session Preview Session=7 Show=false
```

```json
{"ok": true, "result": null}
```

### Destroy

结束 child session 及其 Worker。

```powershell
WUA-CLI.exe CUA Session Destroy Session=7
```

```json
{"ok": true, "result": null}
```

### EnableChildSession

可选的机器设置命令：已配置的机器无需调用。示例为全部设置成功；已是目标值时 changed=false。它不会消除系统密码提示。

```powershell
WUA-CLI.exe CUA Session EnableChildSession
```

```json
{
  "ok": true,
  "result": {
    "child_sessions": {"applied": true, "changed": true, "error": 0},
    "allow_default_credentials": {"applied": true, "changed": true, "error": 0},
    "allow_ntlm_default_credentials": {"applied": true, "changed": true, "error": 0},
    "password_login": {"applied": true, "changed": true, "error": 0}
  }
}
```

## 失败与补充错误

### 主 API 失败

错误摘录；完整失败结果还包括 hresult_text。输入失败后先 Inspect，部分输入可能已经发生，不自动重放。

```powershell
WUA-CLI.exe CUA Window Action=Activate Handle=0
```

```json
{"ok": false, "hresult": -2147024809, "details": "Window requires a nonzero Handle."}
```

### 点击成功，截图保存失败

此示例目录不存在。主操作保持 ok=true；只需重新 Inspect 或保存截图，不应重复点击。补充错误也包含 hresult/hresult_text。

```powershell
WUA-CLI.exe CUA Mouse Operation=Click Handle=00010234 X=400 Y=300 OutFile=missing-directory/after.png
```

```json
{
  "ok": true,
  "result": {
    "image_error": {"ok": false, "details": "The requested PNG could not be captured or saved."},
    "interaction": {"state": "current"}
  }
}
```

### 单独请求截图失败

此时截图属于 Inspect 的主请求，返回 ok=false。

```powershell
WUA-CLI.exe CUA Inspect Handle=00010234 OutFile=missing-directory/after.png
```

```json
{"ok": false, "details": "The requested PNG could not be captured or saved."}
```

成功输入后，补充观察或 Paste 的恢复错误同样只在实际失败时出现在 result.observation_error 或 result.clipboard_error，格式与 image_error 相同；不会撤销已经完成的输入。
