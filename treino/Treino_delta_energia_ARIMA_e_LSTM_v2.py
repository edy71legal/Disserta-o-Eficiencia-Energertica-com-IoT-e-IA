import json, joblib
import numpy as np
import pandas as pd
from google.colab import files
from sklearn.preprocessing import MinMaxScaler
from sklearn.metrics import mean_absolute_error, mean_squared_error, r2_score
from tensorflow.keras.models import Sequential
from tensorflow.keras.layers import LSTM, Dense, Dropout
from statsmodels.tsa.arima.model import ARIMA

print("Faça o upload do arquivo JSON exportado do Firebase:")
uploaded = files.upload()
nome_arquivo = list(uploaded.keys())[0]

def treinar_e_exportar(caminho_json, look_back=10):
    with open(caminho_json, "r", encoding="utf-8") as f:
        dados_json = json.load(f)

    dados_raiz = dados_json.get("leituras", dados_json)
    linhas = []
    for chave, item in sorted(dados_raiz.items(), key=lambda x: str(x[0])):
        if isinstance(item, dict):
            reg = {"timestamp": str(item.get("timestamp", chave))}
            reg.update(item)
            linhas.append(reg)

    df = pd.DataFrame(linhas)
    df.columns = [c.strip().lower() for c in df.columns]

    df["voltagem"] = pd.to_numeric(df.get("voltagem", df.get("tensao", 127.0)), errors="coerce").fillna(127.0)
    df["corrente"] = pd.to_numeric(df.get("corrente", 0.0), errors="coerce").fillna(0.0)
    df["potencia"] = pd.to_numeric(df.get("potencia", 0.0), errors="coerce").fillna(0.0)
    df["consumo_total_pzem"] = pd.to_numeric(df.get("consumo_total_pzem", df.get("energia", 0.0)), errors="coerce").fillna(0.0)
    df["fator_potencia"] = pd.to_numeric(df.get("fator_potencia", df.get("fp", 0.95)), errors="coerce").fillna(0.95)
    df["delta_energia"] = df["consumo_total_pzem"].diff().fillna(0.0).clip(lower=0.0)

    features = ["voltagem", "corrente", "potencia", "consumo_total_pzem", "fator_potencia", "delta_energia"]
    target = ["delta_energia"]

    scaler_x = MinMaxScaler()
    scaler_y = MinMaxScaler()
    X_scaled = scaler_x.fit_transform(df[features])
    y_scaled = scaler_y.fit_transform(df[target])

    train_size = int(len(df) * 0.8)

    def criar_janelas(X_arr, y_arr, lb):
        Xs, ys = [], []
        for i in range(len(X_arr) - lb):
            Xs.append(X_arr[i : i + lb])
            ys.append(y_arr[i + lb])
        return np.array(Xs), np.array(ys)

    X_train, y_train = criar_janelas(X_scaled[:train_size], y_scaled[:train_size], look_back)
    X_test, y_test = criar_janelas(X_scaled[train_size - look_back :], y_scaled[train_size - look_back :], look_back)

    model_lstm = Sequential([
        LSTM(50, return_sequences=True, input_shape=(look_back, len(features))),
        Dropout(0.2),
        LSTM(50, return_sequences=False),
        Dropout(0.2),
        Dense(25, activation="relu"),
        Dense(1),
    ])
    model_lstm.compile(optimizer="adam", loss="mean_squared_error")
    history = model_lstm.fit(X_train, y_train, validation_data=(X_test, y_test), epochs=30, batch_size=16, verbose=1)

    pred_lstm = scaler_y.inverse_transform(model_lstm.predict(X_test)).flatten()
    reais = scaler_y.inverse_transform(y_test).flatten()

    serie_treino_arima = df["delta_energia"].iloc[:train_size].values
    fit_arima = ARIMA(serie_treino_arima, order=(5, 1, 0)).fit()
    pred_arima = np.maximum(0.0, fit_arima.forecast(steps=len(reais)))

    def metricas(y_t, y_p):
        mask = y_t > 1e-6
        mape = float(np.mean(np.abs((y_t[mask] - y_p[mask]) / y_t[mask])) * 100) if np.any(mask) else 0.0
        return {
            "mae": round(float(mean_absolute_error(y_t, y_p)), 5),
            "rmse": round(float(np.sqrt(mean_squared_error(y_t, y_p))), 5),
            "mape": round(mape, 2),
            "r2": round(float(r2_score(y_t, y_p)), 4),
        }

    model_lstm.save("modelo_lstm_multivariado.keras")
    joblib.dump(scaler_x, "scaler_x.pkl")
    joblib.dump(scaler_y, "scaler_y.pkl")

    labels_teste = df["timestamp"].iloc[train_size:].astype(str).str[-8:].tolist()[: len(reais)]
    export_json = {
        "status": "sucesso",
        "labels": labels_teste[-50:],
        "reais": [round(float(v), 5) for v in reais[-50:]],
        "lstm": [round(float(v), 5) for v in pred_lstm[-50:]],
        "arima": [round(float(v), 5) for v in pred_arima[-50:]],
        "loss": [round(float(v), 6) for v in history.history["loss"]],
        "val_loss": [round(float(v), 6) for v in history.history["val_loss"]],
        "metricas": {
            "lstm": metricas(reais, pred_lstm),
            "arima": metricas(reais, pred_arima),
        },
    }

    with open("metricas_treinamento.json", "w", encoding="utf-8") as f:
        json.dump(export_json, f, ensure_ascii=False, indent=2)

    print("Arquivos salvos com sucesso!")

treinar_e_exportar(nome_arquivo)
