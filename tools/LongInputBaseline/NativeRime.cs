using System.Runtime.InteropServices;
using System.Text;

// Same official C ABI used by the earlier isolated CandidateProbe and rime_learning_test.
// Pinned to the project's x64 librime 1.13.1; never attaches to the installed service.
internal static class NativeRime
{
    [StructLayout(LayoutKind.Sequential)] internal struct Traits
    {
        public int size;
        public IntPtr shared, user, name, code, version, app, modules;
        public int minlog;
        public IntPtr logs, prebuilt, staging;
    }
    [StructLayout(LayoutKind.Sequential)] internal struct Composition { public int length, cursor, start, end; public IntPtr preedit; }
    [StructLayout(LayoutKind.Sequential)] internal struct Menu { public int pageSize, pageNo, last, highlight, count; public IntPtr candidates, keys; }
    [StructLayout(LayoutKind.Sequential)] internal struct Candidate { public IntPtr text, comment, reserved; }
    [StructLayout(LayoutKind.Sequential)] internal struct Context { public int size; public Composition composition; public Menu menu; public IntPtr preview, labels; }
    [DllImport("baseline_rime", CallingConvention = CallingConvention.Cdecl)] internal static extern void RimeSetup(ref Traits traits);
    [DllImport("baseline_rime", CallingConvention = CallingConvention.Cdecl)] internal static extern void RimeInitialize(ref Traits traits);
    [DllImport("baseline_rime", CallingConvention = CallingConvention.Cdecl)] internal static extern void RimeFinalize();
    [DllImport("baseline_rime", CallingConvention = CallingConvention.Cdecl)] internal static extern IntPtr RimeGetVersion();
    [DllImport("baseline_rime", CallingConvention = CallingConvention.Cdecl)] internal static extern IntPtr RimeGetUserDataDir();
    [DllImport("baseline_rime", CallingConvention = CallingConvention.Cdecl)] internal static extern int RimeStartMaintenance(int full);
    [DllImport("baseline_rime", CallingConvention = CallingConvention.Cdecl)] internal static extern void RimeJoinMaintenanceThread();
    [DllImport("baseline_rime", CallingConvention = CallingConvention.Cdecl)] internal static extern UIntPtr RimeCreateSession();
    [DllImport("baseline_rime", CallingConvention = CallingConvention.Cdecl)] internal static extern int RimeSelectSchema(UIntPtr session, [MarshalAs(UnmanagedType.LPUTF8Str)] string schema);
    [DllImport("baseline_rime", CallingConvention = CallingConvention.Cdecl)] internal static extern void RimeSetOption(UIntPtr session, [MarshalAs(UnmanagedType.LPUTF8Str)] string option, int value);
    [DllImport("baseline_rime", CallingConvention = CallingConvention.Cdecl)] internal static extern int RimeProcessKey(UIntPtr session, int key, int mask);
    [DllImport("baseline_rime", CallingConvention = CallingConvention.Cdecl)] internal static extern int RimeGetContext(UIntPtr session, ref Context context);
    [DllImport("baseline_rime", CallingConvention = CallingConvention.Cdecl)] internal static extern int RimeFreeContext(ref Context context);
    [DllImport("baseline_rime", CallingConvention = CallingConvention.Cdecl)] internal static extern void RimeClearComposition(UIntPtr session);
    [DllImport("baseline_rime", CallingConvention = CallingConvention.Cdecl)] internal static extern int RimeDestroySession(UIntPtr session);

    internal static IDisposable Open(string shared, string user, bool setup = false)
    {
        var scope = new RimeScope();
        IntPtr Utf8(string text)
        {
            byte[] data = Encoding.UTF8.GetBytes(text + "\0"); IntPtr pointer = Marshal.AllocHGlobal(data.Length);
            scope.Allocations.Add(pointer); Marshal.Copy(data, 0, pointer, data.Length); return pointer;
        }
        try
        {
            var traits = new Traits { size = Marshal.SizeOf<Traits>() - sizeof(int), shared = Utf8(shared), user = Utf8(user), name = Utf8("Synthetic baseline"), code = Utf8("baseline"), version = Utf8("1"), app = Utf8("rime.synthetic_baseline"), minlog = 2, logs = Utf8(user), prebuilt = Utf8(Path.Combine(shared, "build")), staging = Utf8(Path.Combine(user, "build")) };
            if (setup) RimeSetup(ref traits);
            RimeInitialize(ref traits); scope.Initialized = true;
            string actual = Marshal.PtrToStringUTF8(RimeGetUserDataDir()) ?? "";
            if (!Path.GetFullPath(actual).Equals(Path.GetFullPath(user), StringComparison.OrdinalIgnoreCase)) throw new InvalidOperationException("Rime user root isolation failed before session creation");
            return scope;
        }
        catch { scope.Dispose(); throw; }
    }
    sealed class RimeScope : IDisposable
    {
        internal readonly List<IntPtr> Allocations = [];
        internal bool Initialized;
        public void Dispose()
        {
            if (Initialized) { Initialized = false; RimeFinalize(); }
            foreach (var pointer in Allocations) Marshal.FreeHGlobal(pointer);
            Allocations.Clear();
        }
    }
}
