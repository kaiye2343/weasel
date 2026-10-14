using Shorekeeper.Core;

static class SettingsTests
{
    public static void Run(string temp, Action<bool, string> check, Action<Action, string> reject)
    {
        string root = Path.Combine(temp, "generic"); Directory.CreateDirectory(root);
        File.WriteAllText(Path.Combine(root, "default.yaml"), "menu:\n  page_size: 5\nascii_composer:\n  switch_key:\n    Shift_L: inline_ascii\nschema_list:\n  - schema: plain\n  - schema: missing\n");
        File.WriteAllText(Path.Combine(root, "default.custom.yaml"), "patch:\n  menu:\n    page_size: 7\n  ascii_composer/switch_key:\n    Shift_L: commit_text\n    Shift_R: noop\n  unknown: keep\n");
        File.WriteAllText(Path.Combine(root, "plain.schema.yaml"), "schema:\n  schema_id: plain\n  name: 普通方案\n  dependencies: [dependency]\nmenu:\n  page_size: 6\n");
        File.WriteAllText(Path.Combine(root, "plain.custom.yaml"), "patch:\n  unrelated: stable\n");
        var service = new RimeSettings(root);
        var count = RimeSettings.Definitions.Single(d => d.Key == "menu/page_size");
        var shift = RimeSettings.Definitions.Single(d => d.Key.EndsWith("Shift_L"));
        check(service.Read(count, null).Value == "7" && service.Read(count, null).Source == "default.custom.yaml", "nested patch value and source");
        check(service.Read(shift, null).Value == "commit_text", "mixed flat and nested patch read");
        check(service.Read(count, "plain").Value == "6", "scheme overrides global default");
        string before = File.ReadAllText(Path.Combine(root, "default.custom.yaml"));
        var plan = service.Preview(null, [(count, "9"), (shift, "noop")]);
        check(File.ReadAllText(Path.Combine(root, "default.custom.yaml")) == before, "preview does not mutate configuration");
        string backup = plan.Save(root);
        string after = File.ReadAllText(Path.Combine(root, "default.custom.yaml"));
        check(after.Contains("unknown: keep") && after.Contains("Shift_R: noop") && service.Read(shift, null).Value == "noop", "targeted patch preserves siblings and unknown nodes");
        check(File.Exists(Path.Combine(backup, "changes.json")), "save creates reviewed change record");
        RimeSettings.Restore(root, backup); check(File.ReadAllText(Path.Combine(root, "default.custom.yaml")) == before, "configuration restore exact original bytes");
        service = new(root); service.Read(count, null);
        File.AppendAllText(Path.Combine(root, "default.custom.yaml"), "# external change\n");
        reject(() => service.Preview(null, [(count, "8")]), "reject changes since load");
        service = new(root); var raced = service.Preview("plain", [(count, "8")]);
        File.AppendAllText(Path.Combine(root, "plain.custom.yaml"), "# newer edit\n");
        reject(() => raced.Save(root), "reject changes since review");
        service = new(root); reject(() => service.Preview(null, [(count, "11")]), "candidate count validation");
        var model = RimeSettings.Definitions.Single(d => d.Kind == "model");
        check(!service.Read(model, "plain").Supported, "model attachment disabled without schema grammar");
        check(service.Schemas().Any(s => s.Id == "missing" && s.Enabled), "preserve unresolved enabled schemas");
        var schemaPlan = service.PreviewSchemas(["missing", "plain"]); schemaPlan.Save(root);
        check(new RimeSettings(root).Schemas().Where(s => s.Enabled).Select(s => s.Id).SequenceEqual(new[] { "missing", "plain" }), "schema enable order round trip");
        reject(() => service.PreviewSchemas([]), "cannot disable every schema");
        var bindings = RimeSettings.Definitions.Single(d => d.Key == "key_binder/bindings");
        reject(() => new RimeSettings(root).Preview(null, [(bindings, "[{accept: F1}]")]), "validate key binding action and context");
        var punctuation = RimeSettings.Definitions.Single(d => d.Key == "punctuator/half_shape");
        var punctuationPlan = new RimeSettings(root).Preview("plain", [(punctuation, "{'/': '/', '.': ['。', '.']}")]); punctuationPlan.Save(root);
        check(new RimeSettings(root).Read(punctuation, "plain").Value.Contains("'/'"), "punctuation slash key retained");
        string modelFile = Path.Combine(temp, "test-lts.gram"); File.WriteAllText(modelFile, "test-model"); service.ImportModel(modelFile);
        check(service.Models().Contains("test-lts"), "model import and scan");
        reject(() => service.ImportModel(modelFile), "model import never overwrites existing file");
        check(SettingsModules.All.Select(m => m.Id).Distinct().Count() == 10, "module registry independent of skin variants");
    }
}
