# XRPL JPY Stablecoin

Xahau テストネット上で動作する JPY ステーブルコイン（1 トークン = 1 円）の実装です。

## 機能

- トークン発行（1 億トークン）
- 送金（小数点以下不可）
- 残高確認
- トークン焼却（バーン）

## 前提条件

- Node.js
- npm

## セットアップ

1. リポジトリをクローン

```bash
git clone [リポジトリURL]
cd [リポジトリ名]
```

2. 依存関係のインストール

```bash
npm install
```

3. 環境変数の設定
   `.env`ファイルを作成し、以下の内容を設定：

```
XRPL_SEED=あなたのシード
```

## 使用方法

### トークン発行

```javascript
const { issueToken } = require("./jpysc.js");
await issueToken(process.env.XRPL_SEED, "100000000"); // 1億トークン発行
```

### 送金

```javascript
const { sendToken } = require("./jpysc.js");
await sendToken(process.env.XRPL_SEED, "受信者アドレス", "1000"); // 1000トークン送金
```

### 残高確認

```javascript
const { checkBalance } = require("./jpysc.js");
await checkBalance("アドレス");
```

### トークン焼却

```javascript
const { burnToken } = require("./jpysc.js");
await burnToken(process.env.XRPL_SEED, "1000"); // 1000トークン焼却
```

## 注意事項

- 小数点以下の送金はできません
- 送金には受信者の TrustLine 設定が必要です
- テストネットでのみ動作します

## ライセンス

MIT
