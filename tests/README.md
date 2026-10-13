# 验证

`skin_geometry_test.cpp` 直接测试生产代码使用的 `SkinGeometry.h`，覆盖装饰外扩边界、候选宽度变化和200% DPI。

```cmd
cl /nologo /EHsc /W4 tests\skin_geometry_test.cpp /Fe:skin_geometry_test.exe
skin_geometry_test.exe
```

GitHub Actions执行该测试，并完整编译x64与x86的界面通信两端。

原生窗口实测仍需：透明区域连续输入不黑闪；候选变宽时角色跟随；点击、悬停、翻页坐标正确；100/125/150/200% DPI；屏幕四边与跨屏；图片缺失时正常输入；删除skin配置后恢复原界面；输入与工作/日常用户词典不受影响。
