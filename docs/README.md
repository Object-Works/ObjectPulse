# ObjectPulse

ObjectPulse は画面上端の中央にぶら下がる、ノッチ型のステータス表示です。ふだんは画面の上端に隠れていて、マウスを上端に当てたときや知らせることがあるときだけ現れます。
Windows のタスクバーを隠して、その代わりに起動中のアプリの切り替え・バッテリー・クイック設定・通知をまとめて扱えます。
[ObjectFrame](https://github.com/Object-Works/ObjectFrame) と連携して、ワークスペース・レイアウトをノッチに表示します。ワークスペースの切り替えや、マウスが別のモニターへ移ったときには案内のノッチは出しません。

- C++20 / Win32 + Direct2D で書かれた単体の `ObjectPulse.exe`(ランタイム不要)
- 画面の上端とつながった黒いノッチ。バネのアニメーションで形を変えながら、内容がクロスフェードで切り替わります
- ObjectFrame が起動していなくても、タスクバーの代わりとして動きます

---

## 目次

1. [ビルド](#ビルド)
2. [起動と終了](#起動と終了)
3. [表示と操作](#表示と操作)
4. [タスクバーの代わりとして](#タスクバーの代わりとして)
5. [ObjectFrame との連携](#objectframe-との連携)
6. [設定ファイル](#設定ファイル)
7. [連携の仕組み](#連携の仕組み)

---

## ビルド

必要なもの:

- Visual Studio 2022 (「C++ によるデスクトップ開発」ワークロード)
- CMake 3.20 以上

```powershell
cmake -S . -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
```

`build\Release\ObjectPulse.exe` ができます。
実行中の `ObjectPulse.exe` があるとリンクに失敗するので、先に `ObjectPulse.exe quit` で終了してください。

### インストーラー

GitHub Actions ([.github/workflows/build.yml](../.github/workflows/build.yml)) が push のたびに exe をビルドし、
[Inno Setup](https://jrsoftware.org/isinfo.php) でインストーラー (`ObjectPulse-<version>-setup.exe`) とポータブル版 zip を作ります。
どちらも Actions の実行結果の「Artifacts」からダウンロードできます。
`VERSION` と同じ番号のタグ (`v0.1.0` など) を push すると、GitHub の Releases にインストーラーと zip が公開されます。

インストーラーは既定でサインイン時の自動起動を登録し、スタートメニューに「Windows のタスクバーを元に戻す」を追加します。
アンインストール時は ObjectPulse を終了して、タスクバーを元の設定に戻します。
ローカルでインストーラーを作る場合は Inno Setup 6 を入れて次を実行します。

```powershell
& "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe" /DAppVersion=0.1.0 installer\ObjectPulse.iss
```

---

## 起動と終了

```powershell
ObjectPulse.exe                          # 起動 (2 つ目以降は何もせず終了)
ObjectPulse.exe --config D:\my\pulse.conf
ObjectPulse.exe quit                     # 起動中の ObjectPulse を終了 (タスクバーも元に戻ります)
ObjectPulse.exe restore-taskbar          # 終了して、タスクバーを確実に元に戻す
ObjectPulse.exe --version
```

ノッチを右クリックして「ObjectPulse を終了」でも終了できます。

---

## 表示と操作

ObjectPulse には 4 つの状態があります。

| 状態 | 内容 |
| --- | --- |
| 隠れている | 何もないとき (既定の `auto_hide = true`)。画面上端の中央にマウスを当てると展開します |
| 通常 | 小さなノッチ。`auto_hide = false` や `always_small = true` のとき、一時停止中・アプリが注意を求めているときに表示。ワークスペース、時計、バッテリーなど |
| 通知 | レイアウト・一時停止・全画面などが変わったとき、少しの間だけ下に広がって内容を表示。ワークスペースの切り替えと別モニターへの移動では出さない |
| 展開 | マウスを乗せると広がり、フォーカス中のアプリ、タスクバーの機能、ワークスペースとレイアウトのボタンを表示 |

展開したときの並びは次のとおりです。

- 1 段目: フォーカス中 (またはマウスを乗せたアプリ) のアイコン・タイトル、時計と日付
- 2 段目: ピン留めしたアプリ、起動中のアプリ (左から)、右端にクイック設定・通知・バッテリー
- 3 段目 (ObjectFrame 連携時): ワークスペース、レイアウト、一時停止

どの項目を出すかは設定ファイルの [表示内容](#表示内容) で変えられます。

| 操作 | 動作 |
| --- | --- |
| 画面上端の中央にマウスを当てる / ノッチに乗せる | 展開 (表示中のノッチは `expand_on_hover = false` ならクリックで展開) |
| アプリをクリック | そのウィンドウを前面に。前面のものをクリックすると最小化 |
| ピン留めしたアプリをクリック | 起動していなければ起動。ウィンドウがあれば前面に (複数あれば順に切り替え) |
| アプリを中クリック | そのウィンドウを閉じる (ピン留めしたアプリは新しく起動) |
| アプリが入りきらないときの `+N` | タスクビュー (`Win+Tab`) を開く |
| バッテリー・クイック設定 / 通知・時計 | `Win+A` / `Win+N` と同じ |
| ワークスペース・レイアウトのボタン | そのワークスペース / レイアウトへ切り替え、一時停止 / 再開 |
| ホイール | 前 / 次のワークスペース |
| アプリを右クリック | 起動 / 新しいウィンドウ、ピン留めする / 外す、左へ / 右へ移動、ウィンドウを閉じる |
| 右クリック | ノッチと同じ黒いメニュー (ピン留め、タスク マネージャー、タスクバーの表示、ObjectFrame の操作、設定ファイル、終了) |

ほかのモニターで表示中のワークスペースは、ボタンが輪で囲まれます。
マウスを乗せて展開している間は、通知は出ません (展開した表示に同じ内容が出ているため)。

`auto_hide = false` かつ `reserve_space = true` のときは画面上部にノッチの高さぶんの領域を確保するので、最大化したウィンドウや ObjectFrame のタイルがノッチの下に潜りません。

`always_small = true` にすると、上部のノッチは常に小さなまま出ます。マウスを乗せると、通常どおり展開します。ウィンドウは表示中のノッチの大きさだけになり、`reserve_space = true` のときは小さなノッチの高さぴったりを空けます。ノッチの下には余白を足しません。時刻は左、バッテリーは右です。ObjectFrame とつながっていないときは、その2つに合わせた狭い幅になります。展開すると、今アクティブなアプリのアイコンと名前を出します。「ObjectFrame 未接続」は出しません。明るさは各モニターの左端の上下中央、音量とマイクは右端の上下中央に、上部と同じノッチの形で縦に並びます。`always_small` がオフでも同じです。普段は隠れていて、その端にマウスを乗せるとフェーダーまで広がります。フェーダーは上下にドラッグします。展開したノッチでは日付と時刻を横に並べます。クイック設定と通知のボタンは出しません。
全画面のアプリや ObjectFrame の `fullscreen` 中は自動で隠れます。

### ピン留め

起動中のアプリを右クリック →「ピン留めする」で、閉じていても展開したノッチの左側に残ります。

- 起動していないアプリはアイコンだけ、起動中は下にバーが出ます (ウィンドウが複数あるときは少し長いバー)。ピン留めしたアプリのウィンドウはひとつのボタンにまとまります。
- 並び順は右クリック →「左へ移動」「右へ移動」で変えられます。
- ピン留めは設定ファイルと同じフォルダーの `pins.txt` に、1 行に 1 つ exe のパスで保存されます。直接編集して「設定を再読み込み」してもかまいません。
- ストアアプリ (`WindowsApps` 内や `ApplicationFrameHost.exe` で動くもの) は exe から起動できないため、ピン留めできません。

### 表示内容

通常時のノッチと展開時に出す項目は、設定ファイルの `compact_items` と `expanded_items` で選びます。
編集したら、ノッチを右クリック →「設定を再読み込み」で反映されます。

| 表示 | 項目 |
| --- | --- |
| 通常時 (`compact_items`) | `battery`、`attention` (注意を求めているアプリ)、`paused` (一時停止中)、`workspaces`、`clock`、`date` |
| 展開時 (`expanded_items`) | `pins` (ピン留めしたアプリ)、`tasks` (その他の起動中のアプリ)、`battery`、`clock` (時計と日付を横並び)、`workspaces`、`layouts`、`pause` |

通常時のノッチでは、時刻は左端、バッテリーは右端に固定です。それ以外は `compact_items` に書いた順で、その間に並びます。
展開時の項目は決まった位置に表示され、`workspaces`・`layouts`・`pause` をすべて外すと 3 段目がなくなります。

---

## タスクバーの代わりとして

`hide_taskbar = true` (既定) のとき、起動中は Windows のタスクバーを隠します。
タスクバーを「自動的に隠す」設定に切り替えてからウィンドウを非表示にするので、画面の下端まで使えるようになります。
ObjectPulse を終了すると、元の設定に戻してタスクバーを表示します。

- 起動中のアプリは、タスクバーと同じ条件 (表示中で、タスクバーにボタンが出るウィンドウ) で並びます。ObjectFrame が隠している別のワークスペースのウィンドウは出ません。
- 既定の `monitor = all` では、モニターごとにノッチが出て、それぞれそのモニター上のアプリだけを並べます。ワークスペース・レイアウトもそのモニターのものを表示し、ボタンを押すとそのモニターにフォーカスを移してから切り替えます。
- 点滅して注意を求めているアプリは、オレンジで表示され、通常のノッチにもアイコンが出ます。
- 一時的に元のタスクバーを使いたいときは、右クリック →「Windows のタスクバーを表示」。
- 異常終了などでタスクバーが隠れたままになったときは `ObjectPulse.exe restore-taskbar` を実行してください。元の設定は `%USERPROFILE%\.config\objectpulse\taskbar.state` に保存されています。

制限:

- 通知領域 (タスクトレイ) のアイコンは表示できません。必要なときは右クリックからタスクバーを一時的に表示してください。
- ジャンプリストとサムネイルプレビューはありません。ピン留めしていないアプリの起動はキーボードの `Win` キー (スタート) か [ObjectArc](https://github.com/Object-Works/ObjectArc) から行います。
- `monitor` で 1 台を指定したときも、タスクバーはすべてのモニターで隠れます。そのノッチにはすべてのモニターのアプリが並びます。

---

## ObjectFrame との連携

ObjectPulse と ObjectFrame はどちらを先に起動してもかまいません。お互いを見つけると自動でつながり、ObjectFrame を終了するとタスクバーと時計だけの表示に戻ります。

ObjectFrame と一緒に起動するには `ofwm.conf` に次を書きます。

```ini
exec-once = "C:\Tools\ObjectPulse\ObjectPulse.exe"
```

アクセントカラーは既定で ObjectFrame の `border_active` の色を使います (`accent = auto`)。
連携には、`pulse` コマンドに対応した ObjectFrame が必要です。

---

## 設定ファイル

`%USERPROFILE%\.config\objectpulse\pulse.conf` にあります。初回起動時に既定の内容で作られます。
編集したら、ノッチを右クリック →「設定を再読み込み」、または `ObjectPulse reload` で反映されます。
[ObjectManager](https://github.com/Object-Works/ObjectManager) から同じファイルを編集することもできます。

`corners = true` にすると、ノッチを出している各モニターの四隅が、ノッチと同じ黒い角丸で覆われます。半径はノッチの角と同じです。

| キー | 既定値 | 内容 |
| --- | --- | --- |
| `monitor` | `all` | 表示するモニター。`all` (モニターごとに 1 つ)、`primary`、左から数えた番号 (`1`, `2`, ...) |
| `auto_hide` | `true` | 何もないときは隠れ、画面上端の中央にマウスを当てると開く |
| `always_small` | `false` | 常に小さな上部ノッチを出す。ホバーすると展開する。左右の明るさ・音量・マイクは、この設定に関わらず端へホバーしたときだけ出す |
| `reserve_space` | `true` | ノッチを出しっぱなしのとき (`always_small`、または `auto_hide = false`)、画面上部に領域を確保する |
| `hide_taskbar` | `true` | Windows のタスクバーを隠し、ObjectPulse で代わりをする |
| `scale` | `100` | 大きさ (%, 50-200) |
| `expand_on_hover` | `true` | マウスを乗せると展開する |
| `hover_delay` | `150` | 展開するまでの時間 (ms, 0-2000) |
| `notify` | `true` | 変化を通知する |
| `notify_duration` | `1400` | 通知を表示する時間 (ms, 300-10000) |
| `notify_focus` | `false` | フォーカスしたウィンドウが変わったときも通知する |
| `clock_seconds` | `false` | 時計に秒を表示する |
| `hide_on_fullscreen` | `true` | 全画面のときは隠す |
| `accent` | `auto` | アクセントカラー。`auto` (ObjectFrame の色) か `0xRRGGBB` |
| `background` | `0x000000` | ノッチと四隅の色 |
| `opacity` | `100` | 背景の不透明度 (%, 20-100) |
| `corners` | `false` | `true` で、ノッチを出しているモニターの四隅をノッチと同じ半径の角丸で覆う |
| `compact_items` | `battery, attention, paused, workspaces, clock` | 通常時のノッチに出す項目 (書いた順)。`none` で何も出さない |
| `expanded_items` | `pins, tasks, battery, clock, workspaces, layouts, pause` | 展開時に出す項目。日付と時刻は横に並ぶ |

---

## 連携の仕組み

どちらも `WM_COPYDATA` で UTF-16 の文字列を送ります。

**ObjectFrame → ObjectPulse**: ウィンドウクラス `ObjectPulseWnd` に、`dwData = 0x4F46504C` (`'OFPL'`) で `key=value` を 1 行ずつ並べた状態を送ります。
状態が変わるたびに (40ms まとめて) 送られ、同じ内容なら送りません。

| キー | 内容 |
| --- | --- |
| `of` | `1` = 動作中 / `0` = 終了した |
| `monitor` / `monitors` | フォーカス中のモニター番号 (1 から) / モニター数 |
| `workspace` / `workspaces` | 現在のワークスペース (1 から) / ワークスペース数 |
| `occupied` | ウィンドウのあるワークスペース (`1,3,4`) |
| `visible` | いずれかのモニターで表示中のワークスペース |
| `layout` | `dwindle` `master` `monocle` `scroll` |
| `monitor_workspaces` / `monitor_layouts` | モニターごと (左から) のワークスペース / レイアウト (`1,3` / `dwindle,master`) |
| `windows` | 現在のワークスペースのウィンドウ数 |
| `paused` | 一時停止中なら `1` |
| `hwnd` / `exe` / `title` | フォーカス中のウィンドウ |
| `managed` / `floating` / `fullscreen` | フォーカス中のウィンドウがタイル管理中か / フローティングか / 全画面か |
| `accent` | `border_active` の色 (`0xRRGGBB`) |
| `event` | `reload` など、状態に表れない出来事 |

**ObjectPulse → ObjectFrame**: ウィンドウクラス `ObjectFrameWM` に、`dwData = 0x4F46574D` (`'OFWM'`) で `ofwm.exe dispatch` と同じコマンド (`workspace 3` など) を送ります。
起動時には `pulse` コマンドを送って、現在の状態を送り直してもらいます。
