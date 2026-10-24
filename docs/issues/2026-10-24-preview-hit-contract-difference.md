# D1 后续接口差异：网页与原生命中矩形

## 状态与处理约定

2026-10-24，在完成 D1a 图片锚点比较、准备后续候选命中夹具时发现。按用户要求仅整理证据，**没有修改命中逻辑、追加产品插桩或让 DSH 尝试修复**。交由用户调用高级模型确认并处理。

这是已观察到的实现契约差异，尚未通过同一份原生文字布局输出完成跨端复现。不能据此认定用户之前的图片位置或文字基线问题由它导致，也不能声称当前用户实际配置使用了零高亮留白。

## 基线与相关文件

- 分支 `shorekeeper`，发现时源码 `f0eee2bb6c1312da866bda0d3b0bbabb96924a6f`。
- `designer/geometry.js`，`hit`：对候选矩形使用 `x <= c.x+c.width`、`y <= rowTop+c.height`。
- `designer/app.js`：画布选择候选高亮时调用 `G.hit(geometry,p.x,p.y)`。
- `WeaselUI/WeaselPanel.cpp`：点击及悬停路径取得 `GetCandidateRect`，处理窗口／内容原点和重排偏移，再按 `hilite_padding_x/y` 扩张矩形，最后调用 `CRect::PtInRect`。
- `WeaselUI/HorizontalLayout.cpp`：生成原生候选矩形；原生文字／命中事实输出尚未接入共享夹具。

## 具体差异

1. 网页的 `hit` 使用闭合的右／下边界。原生使用 Windows 矩形命中规则，右／下边界不属于矩形内部。即使给出相同的矩形和零扩张，这两种判定也不同。
2. 原生点击／悬停会按高亮留白扩张候选矩形，网页 `hit` 没有显式接收这一参数。有效偏好、DPI 和候选矩形不同的情况下，不能简单拿网页 `cell.width/height` 当作原生命中区。
3. 网页高亮选择是设计器交互，原生是选词交互。高级模型需要先确认哪些部分应共享同一契约，哪些仅是编辑器行为；不应为通过测试直接改其中一端。

## 已执行的最小检查

仅用现有网页模块、合成候选和固定文字测量，在 Node 中调用现有 `G.hit`，没有窗口、键鼠或日常输入法操作。

```javascript
const G = require('./designer/geometry.js');
const P = require('./designer/package.js');
const g = G.layout(
  P.style({annotationPosition:'hidden'}), [],
  [{text:'你好',comment:''}], false,
  (text,font)=>({width:text.length*8,height:28})
);
const c = g.cells[0];
const point = {x:c.x+c.width,y:g.rowTop+1};
console.log(G.hit(g,point.x,point.y));
```

实际输出与数据：

```json
{"rect":[12,8,46,36],"point":{"x":46,"y":9},"browserHit":0,"halfOpenRectContains":false}
```

这里的 `halfOpenRectContains` 是对相同合成矩形做的数学对照，**没有运行原生候选布局或真实窗口事件**。原生实际还需应用高亮留白等处理，当前不据此报告日常选词失败。

## 影响范围与当前门槛

- D1a 的图片锚点／画布比较不涉及命中，已在 CI `40082835762` 的 x64／Win32 步骤中各通过 24 组，不受本问题影响。
- D1 整体仍未完成，尤其不能把 D1a 报告作为“文字／基线／鼠标命中全部一致”的证据。
- 后续固定测量夹具需要独立导出原生候选、绘制和命中矩形，避免为了比较而把网页候选矩形同时喂给两端，形成自我验证。
- 该问题与 C1b 响应程序访问违例分别处理；不修改 IPC／UIStyle、Rime 算法或私人数据。

## 建议高级模型接手时确认

1. 确定共享几何报告中“候选绘制矩形”和“实际选词命中矩形”的准确含义及边界，明确高亮留白和边界重叠时的优先级。
2. 使用原生生产布局与生产命中转换的事实输出，建立相同 DPI／原点下的对照；覆盖右下边界、留白间隙、部分缺注音、透明装饰及重排。
3. 确认设计器是否需要模拟实际选词命中，或明确显示其仅用于编辑器选中；不得隐含宣称两端一致。
4. 根据确认后的契约实现并验证，记录未覆盖部分；不要用容差吞掉布尔命中差异。

接手时保留用户 `tools/SkinFileTrace/README.md` 未提交修改；未经单独授权，不安装、不改日常 Rime 数据、不驱动桌面测试。
