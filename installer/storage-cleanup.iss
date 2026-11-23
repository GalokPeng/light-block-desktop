// All manifests are embedded in Setup. A successful installation is the only
// time we need to inspect retired files. Never enumerate a user's media tree.
function Attributes(Path: String): LongWord;
  external 'GetFileAttributesW@kernel32.dll stdcall';

function PlainPath(Path: String): Boolean;
var
  Parent: String;
  Attr: LongWord;
begin
  Result := False;
  while Path <> '' do begin
    Attr := Attributes(Path);
    if (Attr = $FFFFFFFF) or ((Attr and $400) <> 0) then exit;
    Parent := ExtractFileDir(Path);
    if Parent = Path then break;
    Path := Parent;
  end;
  Result := True;
end;

function HashOf(Path: String): String;
begin
  Result := '';
  if not PlainPath(Path) or not FileExists(Path) then exit;
  try
    Result := Lowercase(GetSHA256OfFile(Path));
  except
    Log('Skipping unavailable file: ' + Path);
  end;
end;

// Both Qt component names carry the first twelve hex digits of their binary
// SHA-256. The taskbar DLL uses a source hash and must not use this rule.
procedure TrimQtComponent(App, PointerFile, Prefix: String);
var
  Data: AnsiString;
  Active, Path, Hash: String;
  Entry: TFindRec;
begin
  if not PlainPath(PointerFile) or not LoadStringFromFile(PointerFile, Data) then exit;
  Active := Trim(String(Data));
  if (Length(Active) <> Length(Prefix) + 16) or (ExtractFileName(Active) <> Active) then exit;
  Hash := HashOf(AddBackslash(App) + Active);
  if (Length(Hash) <> 64) or (Prefix + Copy(Hash, 1, 12) + '.dll' <> Active) then exit;
  if not FindFirst(AddBackslash(App) + Prefix + '*.dll', Entry) then exit;
  try
    repeat
      if (Length(Entry.Name) = Length(Active)) and (CompareText(Entry.Name, Active) <> 0) then begin
        Path := AddBackslash(App) + Entry.Name;
        Hash := HashOf(Path);
        if (Length(Hash) = 64) and (Entry.Name = Prefix + Copy(Hash, 1, 12) + '.dll') then
          DeleteFile(Path);
      end;
    until not FindNext(Entry);
  finally
    FindClose(Entry);
  end;
end;

procedure RetireFiles(App: String);
var
  Lines: TArrayOfString;
  I: Integer;
  Path, Dir: String;
begin
  ExtractTemporaryFile('retired-files.sha256');
  if not LoadStringsFromFile(ExpandConstant('{tmp}\retired-files.sha256'), Lines) then exit;
  for I := 0 to GetArrayLength(Lines) - 1 do begin
    if Copy(Lines[I], 65, 2) = '  ' then begin
      Path := AddBackslash(App) + Copy(Lines[I], 67, MaxInt);
      if (HashOf(Path) = Copy(Lines[I], 1, 64)) and DeleteFile(Path) then begin
        Dir := ExtractFileDir(Path);
        while (CompareText(Dir, App) <> 0) and RemoveDir(Dir) do
          Dir := ExtractFileDir(Dir);
      end;
    end;
  end;
end;

type
  TCachedSetup = record
    Path, Digest: String;
    Version: Int64;
  end;
  TCachedSetups = array of TCachedSetup;

function VersionKey(Value: String): Int64;
var
  I, Split, Part: Integer;
  Token: String;
begin
  Result := -1;
  Value := Value + '.';
  for I := 0 to 2 do begin
    Split := Pos('.', Value);
    if Split = 0 then begin Result := -1; exit; end;
    Token := Copy(Value, 1, Split - 1);
    Delete(Value, 1, Split);
    Part := StrToIntDef(Token, -1);
    if (Part < 0) or (Part > 65535) or (IntToStr(Part) <> Token) then begin Result := -1; exit; end;
    if I = 0 then Result := 0;
    Result := Result * 65536 + Part;
  end;
  if Value <> '' then Result := -1;
end;

function PublishedHash(Known: TStringList; Tag: String): String;
var I: Integer;
begin
  Result := '';
  for I := 0 to Known.Count - 1 do
    if Pos(Tag + '=', Known[I]) = 1 then begin
      Result := Copy(Known[I], Length(Tag) + 2, MaxInt);
      exit;
    end;
end;

procedure ScanCache(Dir: String; Known: TStringList; Current: Int64;
  var Found: TCachedSetups; var Keep: Integer);
var
  Entry: TFindRec;
  Name, Tag, Path, Expected: String;
  V: Int64;
  N: Integer;
begin
  if not PlainPath(Dir) then exit;
  if not FindFirst(AddBackslash(Dir) + 'Qingkuai-v*-Setup-x64.exe', Entry) then exit;
  try
    repeat
      Name := Entry.Name;
      Tag := Copy(Name, 11, Length(Name) - 10 - Length('-Setup-x64.exe'));
      Path := AddBackslash(Dir) + Name;
      V := VersionKey(Tag);
      if (Name = 'Qingkuai-v' + Tag + '-Setup-x64.exe') and (V >= 0) and (V < Current) then begin
        Expected := PublishedHash(Known, Tag);
        if (Length(Expected) = 64) and (HashOf(Path) = Expected) then begin
          N := GetArrayLength(Found);
          SetArrayLength(Found, N + 1);
          Found[N].Path := Path;
          Found[N].Digest := Expected;
          Found[N].Version := V;
          if Keep < 0 then Keep := N
          else if V > Found[Keep].Version then Keep := N;
        end;
      end;
    until not FindNext(Entry);
  finally
    FindClose(Entry);
  end;
end;

procedure TrimSetups(App: String);
var
  Known: TStringList;
  Receipts, Dir, Legacy, OwnHash, Receipt: String;
  Entry: TFindRec;
  Data: AnsiString;
  Found: TCachedSetups;
  Current: Int64;
  I, Keep: Integer;
begin
  Current := VersionKey('{#AppVersion}');
  if Current < 0 then exit;
  Known := TStringList.Create;
  try
    ExtractTemporaryFile('previous-setups.sha256');
    Known.LoadFromFile(ExpandConstant('{tmp}\previous-setups.sha256'));
    // Remember which future Setup actually ran, without doing network I/O.
    Receipts := AddBackslash(App) + 'runtime\setup-receipts';
    if PlainPath(ExtractFileDir(Receipts)) and (DirExists(Receipts) or CreateDir(Receipts))
      and PlainPath(Receipts) then begin
      if FindFirst(Receipts + '\*.sha256', Entry) then begin
        try
          repeat
            Dir := ChangeFileExt(Entry.Name, '');
            Receipt := AddBackslash(Receipts) + Entry.Name;
            if (VersionKey(Dir) >= 0) and (PublishedHash(Known, Dir) = '') and
              (Entry.SizeHigh = 0) and (Entry.SizeLow = 64) and PlainPath(Receipt) and LoadStringFromFile(Receipt, Data) then
                Known.Add(Dir + '=' + String(Data));
          until not FindNext(Entry);
        finally
          FindClose(Entry);
        end;
      end;
      Receipt := AddBackslash(Receipts) + '{#AppVersion}.sha256';
      if (PublishedHash(Known, '{#AppVersion}') = '') and (Attributes(Receipt) = $FFFFFFFF) then begin
        OwnHash := HashOf(ExpandConstant('{srcexe}'));
        if Length(OwnHash) = 64 then SaveStringToFile(Receipt, AnsiString(OwnHash), False);
      end;
    end;
    Keep := -1;
    ScanCache(App + '\data\profile-v2\updates', Known, Current, Found, Keep);
    ScanCache(App + '\data\updates', Known, Current, Found, Keep);
#ifdef TestAppId
    Legacy := App + '\isolated-legacy';
#else
    Legacy := ExpandConstant('{localappdata}\StillWallpaper');
#endif
    ScanCache(Legacy + '\profile-v2\updates', Known, Current, Found, Keep);
    ScanCache(Legacy + '\updates', Known, Current, Found, Keep);
    for I := 0 to GetArrayLength(Found) - 1 do
      if (I <> Keep) and (HashOf(Found[I].Path) = Found[I].Digest) then DeleteFile(Found[I].Path);
  finally
    Known.Free;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssDone then begin
    try
      RetireFiles(ExpandConstant('{app}'));
      TrimQtComponent(ExpandConstant('{app}'), ExpandConstant('{app}\ui-current.txt'), 'LightBlockUi-');
      TrimQtComponent(ExpandConstant('{app}'), ExpandConstant('{app}\runtime\media-current.txt'), 'LightBlockMedia-');
      TrimSetups(ExpandConstant('{app}'));
    except
      Log('Optional disk cleanup skipped: ' + GetExceptionMessage);
    end;
  end;
end;
