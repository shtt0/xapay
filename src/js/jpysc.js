const { xrpl, ISSUER_ADDRESS, CURRENCY_CODE, initClient } = require("./hocks");

// トークン発行
async function issueToken(seed, amount = "1000000") {
  const client = await initClient();
  try {
    const wallet = xrpl.Wallet.fromSeed(seed);

    const trustSetTx = {
      TransactionType: "TrustSet",
      Account: ISSUER_ADDRESS,
      LimitAmount: {
        currency: CURRENCY_CODE,
        issuer: ISSUER_ADDRESS,
        value: amount,
      },
    };

    const prepared = await client.autofill(trustSetTx);
    const signed = wallet.sign(prepared);
    const result = await client.submitAndWait(signed.tx_blob);

    console.log("トークン発行結果:", result);
    return result;
  } catch (error) {
    console.error("エラー:", error);
    throw error;
  } finally {
    await client.disconnect();
  }
}

// 残高確認
async function checkBalance(address) {
  const client = await initClient();
  try {
    const response = await client.request({
      command: "account_lines",
      account: address,
      ledger_index: "current",
    });

    console.log("残高情報:", response.result.lines);
    return response.result.lines;
  } catch (error) {
    console.error("エラー:", error);
    throw error;
  } finally {
    await client.disconnect();
  }
}

// トークン送信
async function sendToken(seed, destination, amount) {
  // 小数点以下のバリデーション
  if (!Number.isInteger(Number(amount))) {
    throw new Error(
      "送金額は整数である必要があります。小数点以下は使用できません。"
    );
  }

  const client = await initClient();
  try {
    const wallet = xrpl.Wallet.fromSeed(seed);

    const paymentTx = {
      TransactionType: "Payment",
      Account: wallet.address,
      Destination: destination,
      Amount: {
        currency: CURRENCY_CODE,
        value: amount,
        issuer: ISSUER_ADDRESS,
      },
    };

    const prepared = await client.autofill(paymentTx);
    const signed = wallet.sign(prepared);
    const result = await client.submitAndWait(signed.tx_blob);

    console.log("送信結果:", result);
    return result;
  } catch (error) {
    console.error("エラー:", error);
    throw error;
  } finally {
    await client.disconnect();
  }
}

// トークン焼却（バーン）
async function burnToken(seed, amount) {
  // 小数点以下のバリデーション
  if (!Number.isInteger(Number(amount))) {
    throw new Error(
      "焼却量は整数である必要があります。小数点以下は使用できません。"
    );
  }

  const client = await initClient();
  try {
    const wallet = xrpl.Wallet.fromSeed(seed);

    // ブラックホールアドレスに送金することで焼却
    const burnTx = {
      TransactionType: "Payment",
      Account: wallet.address,
      Destination: "rrrrrrrrrrrrrrrrrrrrrhoLvTp", // XRPLのブラックホールアドレス
      Amount: {
        currency: CURRENCY_CODE,
        value: amount,
        issuer: ISSUER_ADDRESS,
      },
    };

    const prepared = await client.autofill(burnTx);
    const signed = wallet.sign(prepared);
    const result = await client.submitAndWait(signed.tx_blob);

    console.log("焼却結果:", result);
    return result;
  } catch (error) {
    console.error("エラー:", error);
    throw error;
  } finally {
    await client.disconnect();
  }
}

// 使用例
async function main() {
  // シードを環境変数から取得
  const seed = process.env.XRPL_SEED;
  const amount = "100000000"; // 1億トークン
  if (!seed) {
    console.error(
      "XRPL_SEEDが設定されていません。.envファイルを確認してください。"
    );
    process.exit(1);
  }
  try {
    // トークン発行
    await issueToken(seed, amount);

    // 残高確認
    await checkBalance(ISSUER_ADDRESS);
  } catch (error) {
    console.error("メイン処理でエラー:", error);
  }
}

// スクリプトが直接実行された場合のみmain()を実行
if (require.main === module) {
  main();
}

module.exports = {
  issueToken,
  checkBalance,
  sendToken,
  burnToken,
};
