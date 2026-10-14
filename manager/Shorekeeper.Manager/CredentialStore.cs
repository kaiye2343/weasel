using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;

namespace Shorekeeper.Manager;
internal static class CredentialStore
{
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct Credential
    {
        public uint Flags, Type;
        public string TargetName, Comment;
        public System.Runtime.InteropServices.ComTypes.FILETIME LastWritten;
        public uint CredentialBlobSize;
        public IntPtr CredentialBlob;
        public uint Persist, AttributeCount;
        public IntPtr Attributes;
        public string TargetAlias, UserName;
    }
    [DllImport("advapi32", CharSet = CharSet.Unicode, SetLastError = true)] static extern bool CredWrite(ref Credential credential, uint flags);
    [DllImport("advapi32", CharSet = CharSet.Unicode, SetLastError = true)] static extern bool CredRead(string target, uint type, uint flags, out IntPtr credential);
    [DllImport("advapi32")] static extern void CredFree(IntPtr pointer);
    public static void Save(string target, string username, string password)
    {
        byte[] bytes = Encoding.Unicode.GetBytes(password); IntPtr blob = Marshal.AllocHGlobal(bytes.Length);
        try
        {
            Marshal.Copy(bytes, 0, blob, bytes.Length);
            var credential = new Credential { Type = 1, TargetName = target, Comment = "Shorekeeper WebDAV", UserName = username,
                CredentialBlob = blob, CredentialBlobSize = (uint)bytes.Length, Persist = 2, TargetAlias = "" };
            if (!CredWrite(ref credential, 0)) throw new Win32Exception(Marshal.GetLastWin32Error());
        }
        finally { for (int i = 0; i < bytes.Length; i++) Marshal.WriteByte(blob, i, 0); Marshal.FreeHGlobal(blob); Array.Clear(bytes); }
    }
    public static string? Read(string target)
    {
        if (!CredRead(target, 1, 0, out var pointer)) return null;
        try { var credential = Marshal.PtrToStructure<Credential>(pointer); return Marshal.PtrToStringUni(credential.CredentialBlob, (int)credential.CredentialBlobSize / 2); }
        finally { CredFree(pointer); }
    }
}
