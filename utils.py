import requests
from openai import OpenAI
from PIL import Image
import base64
import io
import json

def get_clima(lat, lon, api_key):
    try:
        url = f"https://api.openweathermap.org/data/2.5/weather?lat={lat}&lon={lon}&units=metric&appid={api_key}&lang=es"
        r = requests.get(url)
        data = r.json()
        print("📡 Respuesta de OpenWeatherMap:", data)

        clima = {
            "temperatura": data["main"]["temp"],
            "sensacion_termica": data["main"]["feels_like"],
            "descripcion": data["weather"][0]["description"],
        }
        return clima
    except Exception as e:
        print("❌ Error al obtener clima:", e)
        return None

def analizar_ropa(imagen_file, api_key, temperatura, descripcion):
    try:
        client = OpenAI(api_key=api_key)

        # Convertir imagen a base64
        img = Image.open(imagen_file)
        print("📸 Tamaño original imagen:", img.size)
        img = img.resize((640, 480))
        buffered = io.BytesIO()
        img.save(buffered, format="JPEG")
        img_base64 = base64.b64encode(buffered.getvalue()).decode()

        # Prompt
        prompt_text = (
            f"La temperatura actual es {temperatura}°C y el clima se describe como '{descripcion}'.\n"
            "La imagen muestra a una persona vestida en un entorno urbano. Tu tarea es evaluar si está bien abrigada para ese clima, "
            "solo considerando la vestimenta visible. Este análisis es para fines informativos y meteorológicos, no de identificación.\n"
            "Devolvé únicamente un JSON con el siguiente formato:\n\n"
            "{\n"
            "  \"ropa_detectada\": [\"campera\", \"pantalón\", \"bufanda\"],\n"
            "  \"esta_abrigado\": true,\n"
            "  \"nivel_abrigado\": \"bien\",\n"
            "  \"comentario\": \"...\",\n"
            "  \"recomendacion\": \"...\"\n"
            "}\n\n"
            "No incluyas explicaciones ni texto adicional, solo el JSON puro."
        )

        print("🧠 Enviando prompt a GPT-4o...")

        response = client.chat.completions.create(
            model="gpt-4o",
            messages=[
                {
                    "role": "user",
                    "content": [
                        {"type": "text", "text": prompt_text},
                        {"type": "image_url", "image_url": {"url": f"data:image/jpeg;base64,{img_base64}"}}
                    ]
                }
            ],
            max_tokens=500
        )

        content = response.choices[0].message.content.strip()
        print("📥 Respuesta bruta GPT:\n", content)

        # Limpiar delimitadores tipo ```json ... ```
        if content.startswith("```"):
            content = content.strip("` \n")
            if content.startswith("json"):
                content = content[4:].strip()

        try:
            resultado = json.loads(content)
            return resultado
        except json.JSONDecodeError as e:
            print("❌ Error al parsear JSON desde GPT:", e)
            print("📦 Contenido recibido:\n", content)
            return None

    except Exception as e:
        print("❌ Error general en analizar_ropa():", e)
    return None
