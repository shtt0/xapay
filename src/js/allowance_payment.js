// Hocks/allowance_payment.js (新規作成を想定)
const { xrpl, initClient } = require("./hocks");
require("dotenv").config({ path: "../.env" });

/**
 * ユーザーが運営者に対して、指定した上限金額までの支払いを許可する署名を生成します。
 * @param {xrpl.Wallet} userWallet - ユーザーのウォレット
 * @param {string} operatorAddress - 許可を与える運営者のアドレス
 * @param {string} allowanceAmount - 許可する上限金額（ドロップ単位）
 * @returns {string} 生成された署名
 */
function createAllowanceSignature(
  userWallet,
  operatorAddress,
  allowanceAmount
) {
  console.log("--- 利用許可署名を生成中 ---");
  // 署名対象のメッセージ: <ユーザーアドレス>:<運営者アドレス>:<許可金額>
  const messageToSign = `${userWallet.address}:${operatorAddress}:${allowanceAmount}`;
  const signature = userWallet.sign(
    xrpl.convertStringToHex(messageToSign)
  ).signature;
  console.log(`生成された利用許可署名: ${signature}`);
  return signature;
}

/**
 * 利用許可署名を使い、運営者がフックをトリガーするトランザクションを送信します。
 * @param {xrpl.Wallet} operatorWallet - 運営者のウォレット
 * @param {string} hookAddress - フックのアドレス
 * @param {string} userAddress - 支払いを行うユーザーのアドレス
 * @param {string} allowanceSignature - 事前にユーザーから得た利用許可署名
 * @param {string} allowanceAmount - 許可された上限金額
 * @param {string} paymentAmount - 今回の支払い金額
 */
async function sendPaymentWithAllowance(
  operatorWallet,
  hookAddress,
  userAddress,
  allowanceSignature,
  allowanceAmount,
  paymentAmount
) {
  const client = await initClient();

  console.log("--- 利用許可署名を使った決済トランザクションを準備中 ---");

  // Memoに格納するデータを構築
  const memoData = {
    // この決済が「利用許可モデル」であることを示すタイプ
    type: "allowance_payment",
    user_address: userAddress,
    payment_amount: paymentAmount,
    allowance: {
      amount: allowanceAmount,
      signature: allowanceSignature.toUpperCase(),
    },
  };

  const invokeTx = {
    TransactionType: "Invoke",
    Account: operatorWallet.address,
    Destination: hookAddress,
    Memos: [
      {
        Memo: {
          MemoData: xrpl.convertStringToHex(JSON.stringify(memoData)),
          MemoFormat: xrpl.convertStringToHex("application/json"),
        },
      },
    ],
  };

  const signedTx = operatorWallet.sign(await client.autofill(invokeTx));
  const result = await client.submitAndWait(signedTx.tx_blob);

  console.log("--- トランザクション結果 ---");
  console.log(result);
  await client.disconnect();
}

// --- 実行部分 ---
async function main() {
  const operatorWallet = xrpl.Wallet.fromSeed(process.env.OPERATOR_SEED);
  const hookAddress = xrpl.Wallet.fromSeed(process.env.HOOK_SEED).address;
  const userWallet = xrpl.Wallet.fromSeed(process.env.USER_SEED_1);

  // 1. ユーザーが運営者(operator)に対し、10000 JPYまでの支払いを許可する
  const allowanceAmount = "10000";
  const allowanceSignature = createAllowanceSignature(
    userWallet,
    operatorWallet.address,
    allowanceAmount
  );

  // ...後日、ユーザーが500 JPYの支払いを要求したと仮定...

  // 2. 運営者は、預かっていた利用許可署名を使って決済を実行
  const paymentAmount = "500";
  await sendPaymentWithAllowance(
    operatorWallet,
    hookAddress,
    userWallet.address,
    allowanceSignature,
    allowanceAmount,
    paymentAmount
  );
}

// スクリプトが直接実行された場合のみmain()を実行
if (require.main === module) {
  main();
}

module.exports = {
  createAllowanceSignature,
  sendPaymentWithAllowance,
};
