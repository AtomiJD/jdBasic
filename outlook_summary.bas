Attribute VB_Name = "OutlookSummary"
'
' outlook_summary.bas — Outlook-VBA-Macro fuer Conversation-Summary via jdBasic.
'
' INSTALLATION:
'   1. Outlook starten, Alt+F11 fuer den VBA-Editor.
'   2. Insert > Module, dann diese Datei reinkopieren.
'   3. JDBASIC_EXE und JDBASIC_SCRIPT unten anpassen, falls jdBasic
'      woanders liegt.
'   4. ANTHROPIC_API_KEY als System-Env-Var setzen (setx ...) und
'      Outlook neu starten.
'   5. Optional: Ribbon-Button anlegen, der diese Sub aufruft, oder
'      einfach Alt+F8 > SummariseConversation > Run.
'
' BENUTZUNG:
'   - Eine Mail aus dem Thread im Explorer/Inspector waehlen.
'   - Macro starten. Es scannt die GANZE Konversation (Outlook-seitig
'     ueber alle Ordner hinweg ueber die Conversation-API).
'   - Wartet auf jdBasic, zeigt das Ergebnis als MsgBox an und schreibt
'     gleichzeitig %TEMP%\jdb_outlook_summary.txt.
'

Option Explicit

' ── Pfade — anpassen falls noetig ────────────────────────────────
Private Const JDBASIC_EXE     As String = "D:\usr\dev\cc\build\jdBasic.exe"
Private Const JDBASIC_SCRIPT  As String = "D:\usr\dev\cc\outlook_summary.jdb"

' Pro-Mail-Body-Limit — Outlook-Conversations koennen aufblaehen.
Private Const MAX_BODY_CHARS  As Long = 5000

Public Sub SummariseConversation()
    On Error GoTo Fail

    Dim selItem As Object
    Set selItem = GetSelectedMailItem()
    If selItem Is Nothing Then
        MsgBox "Bitte zuerst eine E-Mail im Explorer oder Inspector waehlen.", _
               vbExclamation, "outlook_summary"
        Exit Sub
    End If

    Dim conv As Outlook.Conversation
    Set conv = selItem.GetConversation
    If conv Is Nothing Then
        MsgBox "Diese E-Mail hat keine Conversation-Information.", _
               vbExclamation, "outlook_summary"
        Exit Sub
    End If

    ' Conversation-Items via Table-API einsammeln (alle Ordner).
    Dim items As Collection
    Set items = New Collection
    CollectConversationItems conv, items

    If items.Count = 0 Then
        MsgBox "Keine Mails in der Conversation gefunden.", _
               vbExclamation, "outlook_summary"
        Exit Sub
    End If

    ' Chronologisch sortieren — dictionary-trick mit SentOn-Key.
    SortBySent items

    ' JSON bauen + nach %TEMP% schreiben.
    Dim inPath As String, outPath As String
    inPath  = Environ("TEMP") & "\jdb_outlook_thread.json"
    outPath = Environ("TEMP") & "\jdb_outlook_summary.txt"

    ' Falls noch ein alter Output liegt → loeschen, damit wir spaeter
    ' nicht versehentlich eine Stale-Datei lesen.
    On Error Resume Next
    Kill outPath
    On Error GoTo Fail

    WriteThreadJson inPath, selItem.Subject, selItem.ConversationID, items

    ' jdBasic synchron aufrufen (Shell mit WaitOnReturn).
    Dim cmd As String
    cmd = """" & JDBASIC_EXE & """ """ & JDBASIC_SCRIPT & _
          """ """ & inPath & """ """ & outPath & """"

    Dim sh As Object
    Set sh = CreateObject("WScript.Shell")
    Dim rc As Long
    rc = sh.Run("cmd /c """ & cmd & """", 0, True)  ' 0=hidden, True=wait

    If Dir(outPath) = "" Then
        MsgBox "jdBasic hat keine Output-Datei geschrieben." & vbCrLf & _
               "Exit-Code: " & rc & vbCrLf & _
               "Pruefe JDBASIC_EXE / Script-Pfad und ANTHROPIC_API_KEY.", _
               vbCritical, "outlook_summary"
        Exit Sub
    End If

    ' Output einlesen.
    Dim summary As String
    summary = ReadAllText(outPath)

    ' Anzeige — bei langen Texten lieber Notepad statt MsgBox.
    If Len(summary) > 2000 Then
        sh.Run "notepad.exe """ & outPath & """", 1, False
    Else
        MsgBox summary, vbInformation, "Conversation Summary (" & items.Count & " Mails)"
    End If

    Exit Sub

Fail:
    MsgBox "Fehler in SummariseConversation: " & Err.Number & vbCrLf & Err.Description, _
           vbCritical, "outlook_summary"
End Sub


' ── Helpers ──────────────────────────────────────────────────────

Private Function GetSelectedMailItem() As Object
    Dim it As Object
    ' 1) Ein offener Inspector mit MailItem
    If Not Application.ActiveInspector Is Nothing Then
        Set it = Application.ActiveInspector.CurrentItem
        If TypeName(it) = "MailItem" Then
            Set GetSelectedMailItem = it
            Exit Function
        End If
    End If
    ' 2) Aktive Selection im Explorer
    If Not Application.ActiveExplorer Is Nothing Then
        If Application.ActiveExplorer.Selection.Count >= 1 Then
            Set it = Application.ActiveExplorer.Selection.item(1)
            If TypeName(it) = "MailItem" Then
                Set GetSelectedMailItem = it
                Exit Function
            End If
        End If
    End If
    Set GetSelectedMailItem = Nothing
End Function

Private Sub CollectConversationItems(conv As Outlook.Conversation, items As Collection)
    ' Conversation.GetTable liefert eine Table mit EntryID je Mail in der
    ' gesamten Konversation (auch aus Sent Items, Inbox, Subfoldern).
    Dim tbl As Outlook.Table
    Set tbl = conv.GetTable

    Dim row As Outlook.row
    Dim ns As Outlook.NameSpace
    Set ns = Application.GetNamespace("MAPI")

    Do While Not tbl.EndOfTable
        Set row = tbl.GetNextRow()
        Dim entryID As String
        entryID = row("EntryID")
        On Error Resume Next
        Dim mi As Object
        Set mi = ns.GetItemFromID(entryID)
        If Not mi Is Nothing Then
            If TypeName(mi) = "MailItem" Then
                items.Add mi
            End If
        End If
        On Error GoTo 0
    Loop
End Sub

Private Sub SortBySent(items As Collection)
    ' Naive Insertion-Sort — Conversations sind selten >100 Mails.
    Dim i As Long, j As Long
    For i = 2 To items.Count
        Dim cur As Object
        Set cur = items.item(i)
        Dim curDate As Date
        curDate = cur.SentOn
        j = i - 1
        Do While j >= 1
            If items.item(j).SentOn > curDate Then
                Exit Do
            End If
            j = j - 1
        Loop
        ' cur an Position j+1 einfuegen, von altem Platz entfernen.
        items.Remove i
        If j + 1 > items.Count Then
            items.Add cur
        Else
            items.Add cur, before:=j + 1
        End If
    Next i
End Sub

Private Sub WriteThreadJson(path As String, subject As String, _
                             threadID As String, items As Collection)
    Dim sb As String
    sb = "{" & vbCrLf
    sb = sb & "  ""subject"": " & JsonString(subject) & "," & vbCrLf
    sb = sb & "  ""thread"":  " & JsonString(threadID) & "," & vbCrLf
    sb = sb & "  ""messages"": [" & vbCrLf

    Dim first As Boolean
    first = True
    Dim mi As Object
    For Each mi In items
        If Not first Then sb = sb & "," & vbCrLf
        first = False

        Dim body As String
        body = mi.Body
        If Len(body) > MAX_BODY_CHARS Then
            body = Left$(body, MAX_BODY_CHARS) & vbCrLf & _
                   "[... gekuerzt, Original " & Len(mi.Body) & " Zeichen ...]"
        End If

        sb = sb & "    {" & vbCrLf
        sb = sb & "      ""from"": " & JsonString(mi.SenderName & " <" & mi.SenderEmailAddress & ">") & "," & vbCrLf
        sb = sb & "      ""to"":   " & JsonString(CStr(mi.To)) & "," & vbCrLf
        sb = sb & "      ""cc"":   " & JsonString(CStr(mi.CC)) & "," & vbCrLf
        sb = sb & "      ""date"": " & JsonString(Format(mi.SentOn, "yyyy-mm-dd hh:nn")) & "," & vbCrLf
        sb = sb & "      ""body"": " & JsonString(body) & vbCrLf
        sb = sb & "    }"
    Next mi

    sb = sb & vbCrLf & "  ]" & vbCrLf
    sb = sb & "}" & vbCrLf

    ' Als UTF-8 schreiben — OHNE BOM. ADODB.Stream charset=utf-8 schreibt
    ' standardmaessig ein EF BB BF am Anfang, was viele JSON-Parser (auch
    ' jdBasic vor dem Fix) nicht erwarten. Trick: in Text-Stream schreiben,
    ' Position auf 3 setzen, ueber adTypeBinary in Binaer-Stream kopieren,
    ' speichern.
    Dim txt As Object, bin As Object
    Set txt = CreateObject("ADODB.Stream")
    txt.Type = 2  ' adTypeText
    txt.Charset = "utf-8"
    txt.Open
    txt.WriteText sb

    Set bin = CreateObject("ADODB.Stream")
    bin.Type = 1  ' adTypeBinary
    bin.Open
    txt.Position = 3  ' BOM ueberspringen
    txt.Type = 1      ' Stream auf binaer umschalten zum Kopieren
    txt.CopyTo bin
    bin.SaveToFile path, 2  ' adSaveCreateOverWrite
    bin.Close
    txt.Close
End Sub

Private Function JsonString(s As String) As String
    ' RFC 8259 minimal: ", \, control chars escapen.
    Dim out As String
    out = """"
    Dim i As Long, ch As String, code As Long
    For i = 1 To Len(s)
        ch = Mid$(s, i, 1)
        code = AscW(ch)
        Select Case code
            Case 34:  out = out & "\"""        ' "
            Case 92:  out = out & "\\"         ' \
            Case 8:   out = out & "\b"
            Case 9:   out = out & "\t"
            Case 10:  out = out & "\n"
            Case 12:  out = out & "\f"
            Case 13:  out = out & "\r"
            Case Is < 32
                out = out & "\u" & Right$("0000" & Hex$(code), 4)
            Case Else
                out = out & ch
        End Select
    Next i
    out = out & """"
    JsonString = out
End Function

Private Function ReadAllText(path As String) As String
    ' UTF-8 zurueck-lesen, damit jdBasics CHR$(10)-Trennungen sauber landen.
    Dim stream As Object
    Set stream = CreateObject("ADODB.Stream")
    stream.Type = 2
    stream.Charset = "utf-8"
    stream.Open
    stream.LoadFromFile path
    ReadAllText = stream.ReadText
    stream.Close
End Function
