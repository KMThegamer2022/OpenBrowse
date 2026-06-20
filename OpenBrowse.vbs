Set shell = CreateObject("WScript.Shell")
Set fso = CreateObject("Scripting.FileSystemObject")

baseDir = fso.GetParentFolderName(WScript.ScriptFullName)
launcherPath = fso.BuildPath(baseDir, "launch_openbrowse.bat")

If Not fso.FileExists(launcherPath) Then
    MsgBox "OpenBrowse could not find launch_openbrowse.bat.", _
           vbCritical, "OpenBrowse"
    WScript.Quit 1
End If

shell.CurrentDirectory = baseDir
shell.Run "cmd /c """ & launcherPath & """", 0, False
