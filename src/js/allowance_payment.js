// Hocks/allowance_payment.js (新規作成を想定)
const { xrpl, ISSUER_ADDRESS, CURRENCY_CODE, initClient } = require("./hocks");
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
 * 指定された額をチャージし、利用許可枠（アローワンス）を動的に更新するトランザクションを送信します。
 * @param {xrpl.Wallet} userWallet - ユーザーのウォレット
 * @param {string} hookAddress - 決済フックのアカウントアドレス
 * @param {string} operatorAddress - サービス運営者のアドレス
 * @param {string} chargeAmount - チャージするJPYSCの額
 * @param {string} remainingAllowance - 現在の利用許可枠の残額
 * @returns {Promise<Object>} トランザクション結果
 */
async function chargeAndUpdateAllowance(
  userWallet,
  hookAddress,
  operatorAddress,
  chargeAmount,
  remainingAllowance
) {
  const client = await initClient();
  try {
    // 1. 新しい利用許可枠の合計額を計算
    const newAllowanceAmount = (
      parseFloat(remainingAllowance) + parseFloat(chargeAmount)
    ).toString();

    // 2. 新しい利用許可枠に対する署名を生成
    const newSignature = createAllowanceSignature(
      userWallet,
      operatorAddress,
      newAllowanceAmount
    );

    // 3. トランザクションを構築
    const tx = {
      TransactionType: "Invoke",
      Account: userWallet.address,
      Destination: hookAddress,
      Amount: {
        currency: CURRENCY_CODE,
        issuer: ISSUER_ADDRESS,
        value: chargeAmount,
      },
      Memos: [
        {
          Memo: {
            MemoData: xrpl.convertStringToHex(
              JSON.stringify({
                type: "update_allowance",
                allowance: newAllowanceAmount,
                signature: newSignature,
              })
            ),
            MemoFormat: xrpl.convertStringToHex("application/json"),
          },
        },
      ],
    };

    console.log("--- スマートチャージトランザクションを送信中 ---");
    console.log("チャージ額:", chargeAmount);
    console.log("新しい利用許可枠:", newAllowanceAmount);

    // 4. トランザクションに署名して送信
    const prepared = await client.autofill(tx);
    const signed = userWallet.sign(prepared);
    const result = await client.submitAndWait(signed.tx_blob);

    console.log("--- トランザクション結果 ---");
    console.log(result);
    return result;
  } catch (error) {
    console.error("エラー:", error);
    throw error;
  } finally {
    await client.disconnect();
  }
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

/**
 * フックに預けた残高の一部または全部を引き出すトランザクションを送信します。
 * @param {XrplClient} client - XRPLクライアント
 * @param {Account} userWallet - ユーザーのウォレット情報（アドレスと秘密鍵）
 * @param {string} hookAddress - 決済フックのアカウントアドレス
 * @param {string} withdrawAmount - 引き出すJPYSCの額
 */
export async function withdrawBalance(
  client,
  userWallet,
  hookAddress,
  withdrawAmount
) {
  // 1. トランザクションを構築
  const tx = {
    TransactionType: "Invoke",
    Account: userWallet.address,
    Destination: hookAddress,
    Memos: [
      // Memoフィールドに引き出し要求の情報を格納
      {
        Memo: {
          MemoData: Buffer.from(
            JSON.stringify({
              type: "withdraw", // フック側で処理を識別するためのタイプ
              amount: withdrawAmount,
            })
          ).toString("hex"),
        },
      },
    ],
  };

  console.log("Submitting withdrawal transaction:", tx);

  // 2. トランザクションに署名して送信
  const result = await client.send(
    { ...tx, Account: userWallet.address },
    { wallet: userWallet }
  );
  console.log("Transaction result:", result);
  return result;
}

// --- 実行部分 ---
async function main() {
  const operatorWallet = xrpl.Wallet.fromSeed(process.env.OPERATOR_SEED);
  const hookAddress = xrpl.Wallet.fromSeed(process.env.HOOK_SEED).address;
  const userWallet = xrpl.Wallet.fromSeed(process.env.USER_SEED_1);

  // スマートチャージの例
  const chargeAmount = "2000";
  const currentAllowance = "5000";
  await chargeAndUpdateAllowance(
    userWallet,
    hookAddress,
    operatorWallet.address,
    chargeAmount,
    currentAllowance
  );

  // 従来の支払い処理の例
  const allowanceAmount = "10000";
  const allowanceSignature = createAllowanceSignature(
    userWallet,
    operatorWallet.address,
    allowanceAmount
  );

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
  chargeAndUpdateAllowance,
  withdrawBalance,
};
