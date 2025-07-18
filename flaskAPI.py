import os
from flask import Flask, request, jsonify
from utils import get_clima, analizar_ropa
from dotenv import load_dotenv

# Cargar claves desde .env
OPENAI_API_KEY = os.getenv("OPENAI_API_KEY")
OWM_API_KEY = os.getenv("OWM_API_KEY")

app = Flask(__name__)

@app.route("/evaluar_abrigado", methods=["POST"])
def evaluar_abrigado():
    lat = request.form.get("lat")
    lon = request.form.get("lon")
    imagen = request.files.get("imagen")

    print("📥 Datos recibidos - lat:", lat, "| lon:", lon, "| imagen:", "sí" if imagen else "no")

    if not lat or not lon or not imagen:
        return jsonify({"error": "Faltan datos: lat, lon o imagen"}), 400

    # Paso 1: obtener clima
    clima = get_clima(lat, lon, OWM_API_KEY)
    if not clima:
        print("❌ Error al obtener clima para:", lat, lon)
        return jsonify({"error": "Error obteniendo clima"}), 500

    print("🌡️ Clima recibido:", clima)

    # Paso 2: analizar ropa
    resultado_gpt = analizar_ropa(
        imagen,
        OPENAI_API_KEY,
        clima["sensacion_termica"],
        clima["descripcion"]
    )

    if resultado_gpt is None:
        print("⚠️ El resultado de GPT fue None.")
        return jsonify({"error": "Error al analizar imagen"}), 500

    print("✅ Análisis completado:", resultado_gpt)

    return jsonify({
        "clima": clima,
        **resultado_gpt
    })

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000)
