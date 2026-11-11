using System;
using System.Collections.Generic;
using System.IO;
using Microsoft.Win32;

namespace Nebula.Setup
{
    /// <summary>
    /// An installed upstream Nebula, which a first install of NebulaCoop can take its packed game, saves and settings from
    /// (only copies: Nebula's own folders are never changed). The player names either of Nebula's folders: its install
    /// folder (%LOCALAPPDATA%\Programs\Nebula, which holds the packed game) or its data folder (%LOCALAPPDATA%\Nebula, the
    /// saves and settings); the other is found from it.
    /// </summary>
    internal sealed class NebulaImport
    {
        /// <summary>What a data-folder copy leaves behind: caches, logs, crash reports and this machine's sessions, which
        /// NebulaCoop makes again; and the backups Setup and the launcher keep of Nebula's own versions.</summary>
        private static readonly string[] SkippedFolders =
        {
            "shadercache", "logs", "crashes", "diagnostics", "setup-logs", "sessions", "instances", "update-backup"
        };
        private static readonly string[] SkippedFiles = { "rt-prepare.request" };

        /// <summary>Nebula's install folder (its packed game); null when none was found.</summary>
        public readonly string InstallRoot;
        /// <summary>Nebula's data folder (saves and settings); null when none was found.</summary>
        public readonly string DataRoot;

        private NebulaImport(string installRoot, string dataRoot)
        {
            InstallRoot = installRoot;
            DataRoot = dataRoot;
        }

        /// <summary>Upstream Nebula's own install-root key (Software\Nebula).</summary>
        private static string RegisteredUpstreamRoot
        {
            get
            {
                try
                {
                    using (var key = Registry.CurrentUser.OpenSubKey(@"Software\Nebula"))
                    {
                        var value = key == null ? null : key.GetValue("InstallRoot") as string;
                        return string.IsNullOrEmpty(value) ? null : value;
                    }
                }
                catch (Exception) { return null; }
            }
        }

        /// <summary>The folder Setup offers first: Nebula's install folder when there is one, else its data folder.</summary>
        public static string DefaultFolder
        {
            get
            {
                foreach (var candidate in new[] { RegisteredUpstreamRoot, Path.Combine(NebulaPaths.LocalAppData, "Programs", "Nebula") })
                    if (candidate != null && IsInstallRoot(candidate)) return candidate;
                return NebulaPaths.UpstreamDataRoot;
            }
        }

        private static bool IsInstallRoot(string folder)
        {
            return File.Exists(Path.Combine(folder, "current.json")) || Directory.Exists(Path.Combine(folder, "content"));
        }

        private static bool IsDataRoot(string folder)
        {
            return Directory.Exists(Path.Combine(folder, "saves")) || File.Exists(Path.Combine(folder, "settings.json")) ||
                   File.Exists(Path.Combine(folder, "config.ini"));
        }

        /// <summary>Nebula's folders, from either one the player chose; throws with a message for the player when the folder is
        /// neither, or is this install's own.</summary>
        public static NebulaImport Locate(string folder)
        {
            if (string.IsNullOrEmpty(folder) || !Directory.Exists(folder))
                throw new InvalidOperationException(NebulaText.Of("The Nebula folder was not found: {0}", folder));
            string full = Path.GetFullPath(folder).TrimEnd('\\', '/');
            if (string.Equals(full, Path.GetFullPath(NebulaPaths.DataRoot).TrimEnd('\\', '/'), StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException(NebulaText.Of("Choose Nebula's folder, not NebulaCoop's own."));
            string installRoot = null, dataRoot = null;
            if (IsInstallRoot(full))
            {
                installRoot = full;
                if (IsDataRoot(NebulaPaths.UpstreamDataRoot)) dataRoot = NebulaPaths.UpstreamDataRoot;
            }
            else if (IsDataRoot(full))
            {
                dataRoot = full;
                foreach (var candidate in new[] { RegisteredUpstreamRoot, Path.Combine(NebulaPaths.LocalAppData, "Programs", "Nebula") })
                    if (candidate != null && IsInstallRoot(candidate)) { installRoot = Path.GetFullPath(candidate); break; }
            }
            else
            {
                throw new InvalidOperationException(NebulaText.Of("{0} holds no Nebula game, saves or settings. Choose Nebula's install folder or its data folder.", full));
            }
            return new NebulaImport(installRoot, dataRoot);
        }

        /// <summary>Nebula's install as a layout; null without one.</summary>
        public InstallLayout Layout { get { return InstallRoot == null ? null : new InstallLayout(InstallRoot); } }

        /// <summary>
        /// Copies Nebula's saves and settings into NebulaCoop's data folder. A file NebulaCoop already has is left as it is, so
        /// running this again never undoes play. Returns how many files were copied.
        /// </summary>
        public int CopyUserData(Action<string> log)
        {
            if (DataRoot == null) return 0;
            string from = Path.GetFullPath(DataRoot).TrimEnd(Path.DirectorySeparatorChar);
            string to = Path.GetFullPath(NebulaPaths.DataRoot);
            int copied = 0;
            var skipped = new HashSet<string>(SkippedFolders, StringComparer.OrdinalIgnoreCase);
            var pending = new Stack<string>();
            pending.Push(from);
            while (pending.Count > 0)
            {
                string dir = pending.Pop();
                foreach (var child in Directory.GetDirectories(dir))
                {
                    string name = Path.GetFileName(child);
                    // Top-level caches, logs and backups ("portable-<version>" is the launcher's copy of a portable run).
                    if (dir == from && (skipped.Contains(name) || name.StartsWith("portable-", StringComparison.OrdinalIgnoreCase))) continue;
                    if ((File.GetAttributes(child) & FileAttributes.ReparsePoint) != 0) continue;
                    pending.Push(child);
                }
                foreach (var file in Directory.GetFiles(dir))
                {
                    if (dir == from && Array.IndexOf(SkippedFiles, Path.GetFileName(file)) >= 0) continue;
                    string target = Path.Combine(to, file.Substring(from.Length + 1));
                    if (File.Exists(target)) continue;
                    Directory.CreateDirectory(Path.GetDirectoryName(target));
                    File.Copy(file, target, false);
                    copied++;
                }
            }
            log("Copied " + copied + " files of saves and settings from " + from + " to " + to + ".");
            return copied;
        }
    }
}
