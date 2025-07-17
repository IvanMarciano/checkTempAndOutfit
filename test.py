import requests

with open("foto.jpg", "rb") as f:
    response = requests.post("http://localhost:5000/analizar_ropa", files={"imagen": f})

print(response.json())
