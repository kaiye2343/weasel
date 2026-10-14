using System.Windows.Controls;
using Shorekeeper.Core;

namespace Shorekeeper.Manager;
/// <summary>Registration is for application modules; it does not execute third-party binaries.</summary>
public sealed class SettingsPageRegistry
{
    readonly Dictionary<string, (SettingsModule Module, Func<UserControl> Factory)> entries = [];
    public IReadOnlyList<SettingsModule> Modules => entries.Values.Select(e => e.Module).ToList();
    public void Register(SettingsModule module, Func<UserControl> factory)
    {
        ArgumentNullException.ThrowIfNull(factory);
        if (!entries.TryAdd(module.Id, (module, factory))) throw new ArgumentException("设置模块重复：" + module.Id);
    }
    public UserControl Create(string id) => entries.TryGetValue(id, out var entry) ? entry.Factory() : throw new ArgumentException("未知设置模块：" + id);
}
