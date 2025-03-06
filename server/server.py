import sys
print(f"Ejecutando con Python {sys.version}")

from flask import Flask, render_template, request, jsonify
import os
import cv2
import torch
import numpy as np
import json
import boto3
from datetime import datetime
from AWSIoTPythonSDK.MQTTLib import AWSIoTMQTTClient
from flask_cors import CORS
import cloudinary
import cloudinary.uploader
import os
from dotenv import load_dotenv
from supabase import create_client, Client

SUPABASE_URL = os.getenv("SUPABASE_ACCES_URL")
SUPABASE_KEY = os.getenv("SUPABASE_ACCES_KEY")

supabase: Client = create_client(SUPABASE_URL, SUPABASE_KEY)

# Configurar Cloudinary
cloudinary.config(
    cloud_name= os.getenv("CLOUD_NAME"),
    api_key=os.getenv("API_ACCES_KEY"),
    api_secret= os.getenv("API_ACCES_SECRET")
)


app = Flask('__name__')
CORS(app)

UPLOAD_FOLDER = os.getenv("UPLOADFOLDER")
os.makedirs(UPLOAD_FOLDER, exist_ok=True)

# Cargar modelo YOLOv5
model = torch.hub.load('ultralytics/yolov5', 'yolov5s', pretrained=True)

# Configuración de AWS
client_id = "ESP32-CAM-Detector"
endpoint = os.getenv("ENDPOINT")
root_ca = os.getenv("AMAZONROOTCA")
private_key = os.getenv("PRIVATEKEY")
certificate = os.getenv("CERTIFICATE")


mqtt_client = AWSIoTMQTTClient(client_id)
mqtt_client.configureEndpoint(endpoint, 8883)
mqtt_client.configureCredentials(root_ca, private_key, certificate)
mqtt_client.connect()

contador_personas = 0
datos_deteccion = []  # Lista para almacenar los datos

@app.route('/upload', methods=['POST'])
def upload():
    global contador_personas, datos_deteccion

    if 'file' not in request.files and request.data == b'':
        return "No se recibió imagen", 400

    image_data = request.data

    if not image_data:
        return "Imagen vacía", 400

    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    temp_filename = f"temp_{timestamp}.jpg"
    temp_filepath = os.path.join(UPLOAD_FOLDER, temp_filename)

    with open(temp_filepath, "wb") as f:
        f.write(image_data)

    img = cv2.imread(temp_filepath)
    results = model(img)
    detected_classes = results.pandas().xyxy[0]['name'].tolist()

    if 'person' in detected_classes:
        cantidad_personas = detected_classes.count("person")

        # Subir la imagen a Cloudinary
        upload_result = cloudinary.uploader.upload(temp_filepath)
        image_url = upload_result["secure_url"]  # URL de la imagen en Cloudinary

        

        print(f"Persona detectada. Imagen subida a: {image_url}")

        payload = {
            "tienda": "Supermercado Tottus",
            "hora": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
            "cantidad": cantidad_personas,
            "sucursal": "Sucursal Callao",
            "imagen": image_url  # Agregar la URL de la imagen
        }

        # Enviar datos a AWS IoT Core
        mqtt_client.publish("deteccion", json.dumps(payload), 1)
        print(f"Datos enviados a AWS IoT Core: {payload}")

        # Insertar datos en Supabase
        try:
            response = supabase.table("detecciones").insert(payload).execute()
            print("Datos guardados en Supabase:", response)
        except Exception as e:
            print(f"Error al guardar en Supabase: {e}")

        # Guardar en la lista de datos
        datos_deteccion.append(payload)

        return jsonify({"mensaje": "Persona detectada", "imagen": image_url}), 200
    else:
        os.remove(temp_filepath)
        print("No se detectó persona, imagen descartada")
        return "No se detectó persona, imagen descartada", 200


@app.route('/data', methods=['GET'])
def get_data():
    return jsonify(datos_deteccion), 200


@app.route('/')
def index():
    return render_template('index.html')

@app.route('/latest.jpg', methods=['GET'])
def get_latest_image():
    if len(datos_deteccion) == 0:
        return jsonify({"url": None}), 404

    ultima_imagen = datos_deteccion[-1]["imagen"].replace('\\', '/')
    url = f"/imagenes/{os.path.basename(ultima_imagen)}"
    return jsonify({"url": url})


if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, debug=True)
