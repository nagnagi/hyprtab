# hyprtab

Hyprland 用の Alt-Tab 風ウィンドウスイッチャー。Qt6 + QML（Open Qt）で実装した
オーバーレイGUIがウィンドウ一覧を横並びで表示し、選択したウィンドウへ
フォーカスを切り替える。

## ビルド

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
# build/hyprtab が生成される。PATH の通った場所に置くか、フルパスで使う
```

## 使い方

GUIプロセスを常駐させる（引数なしで起動）:

```sh
hyprtab &
```

CLIからコマンドを送る（`hyprtab <command>`。GUIが `$XDG_RUNTIME_DIR/hyprtab.sock`
で待ち受ける）:

| コマンド | 動作 |
|---|---|
| `show` | 一覧を表示（MRUの2番目を選択状態にする） |
| `next` / `prev` | 非表示なら表示してから選択を進める／戻す |
| `select <index>` | 指定位置を選択 |
| `commit` | 選択中のウィンドウへフォーカスを移して閉じる |
| `cancel` / `hide` | フォーカスを変えずに閉じる |
| `activate <address>` | GUIを介さず直接フォーカス |
| `list` | MRU順の一覧を標準出力へ（デバッグ用） |
| `refresh` | 一覧を再取得 |
| `ping` | 死活確認 |

## 表示仕様

全画面オーバーレイではなく、fuzzel のように**フォーカス中モニタの中央に
浮く小さなパネル**（デフォルト 920x300）として表示する。
アプリ自身が表示時に float + center を hyprctl で適用するため設定なしでも浮くが、
サイズまで固定したい場合は windowrule を推奨:

```conf
windowrulev2 = float, class:^(hyprtab.desktop)$
windowrulev2 = center, class:^(hyprtab.desktop)$
windowrulev2 = size 920 300, class:^(hyprtab.desktop)$
```

パネル以外をクリックしてフォーカスが外れると、切り替えずに自動で閉じる
（fuzzel と同じ挙動）。

## Hyprland の設定例

`~/.config/hypr/hyprland.conf` に以下を追記する。

```conf
# セッション起動時にGUIを常駐させる
exec-once = hyprtab

# Alt+Tab でスイッチャーを開き、Tab を押すたびに選択を進める
bindr = ALT, Tab, exec, hyprtab next
bindr = ALT, Shift, Tab, exec, hyprtab prev
bind = ALT, Return, exec, hyprtab commit
bind = ALT, Escape, exec, hyprtab cancel
```

オーバーレイがフォーカスを持っている間は Alt キーのリリースを QML 側で検出して
`commit` するため、Alt を離すとその時点の選択ウィンドウへ切り替わる。
マウスでカードをクリックしても決定できる。

submap を使う場合は次でもよい（`release` はサブマップ内でキーが離されたときに発火）:

```conf
bind = ALT, Tab, submap, hyprtab
submap = hyprtab
bind = , Tab, exec, hyprtab next
bind = Shift, Tab, exec, hyprtab prev
bind = , Return, exec, hyprtab commit
bind = , Escape, exec, hyprtab cancel
bind = , release, exec, hyprtab commit
submap = reset
```

## 仕組み

- `hyprctl clients -j` を `focusHistoryID` 昇順（0が現在のフォーカス=MRU順）で
  並べ替えて一覧モデルを構築する。自分自身（app_id `hyprtab.desktop`）と
  special ワークスペースのウィンドウは除外する。
- 表示中は500ms間隔で一覧を再取得し、選択はアドレス基準で維持する。
  フォーカスが自分以外に移って1秒以上経つと自動で閉じる（キャンセル扱い）。
- 表示時に自分のウィンドウへフォーカスを移し（キー入力受付のため）、
  float + center + サイズ再設定を hyprctl で適用する。
- フォーカス切替は `hyprctl dispatch focuswindow address:...` で行う。
  Hyprland 0.57 以降は hyprctl が Lua ベースになったため、旧構文が失敗したら
  `hl.dsp.focus({window=hl.get_window("address:...")})` 形式にフォールバックする。
- アイコンは `class` / `initialClass` からアイコンテーマを検索する
  （`image://hypricon/` プロバイダ）。
