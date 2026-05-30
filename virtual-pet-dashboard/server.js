const express = require('express');
const http = require('http');
const { Server } = require('socket.io');
const mqtt = require('mqtt');
const path = require('path');

const app = express();
const server = http.createServer(app);
const io = new Server(server);

// Configuração MQTT para o broker local
const MQTT_BROKER = 'mqtt://10.229.103.1:1883';
const mqttClient = mqtt.connect(MQTT_BROKER);

mqttClient.on('connect', () => {
    console.log('Backend Node.js conectado ao MQTT Broker local!');
    mqttClient.subscribe('virtualpet/status');
});

mqttClient.on('message', (topic, message) => {
    if (topic === 'virtualpet/status') {
        try {
            const payloadString = message.toString();
            console.log(`[MQTT RECEBIDO] Tópico: ${topic} | Payload: ${payloadString}`);
            const data = JSON.parse(payloadString);
            io.emit('pet_status', data); // Encaminha o JSON do ESP32 para o Frontend via WebSocket
        } catch (e) {
            console.error('Erro a fazer parse do JSON:', e);
        }
    }
});

// Servir os ficheiros estáticos (Frontend)
app.use(express.static(path.join(__dirname, 'public')));

// WebSockets (Frontend -> Node.js -> MQTT)
io.on('connection', (socket) => {
    console.log('Browser conectado à dashboard');
    
    // Recebe o comando do browser
    socket.on('command', (cmd) => {
        if (cmd.action === 'play') {
            console.log('Comando "play" recebido da dashboard! A enviar para o ESP32 via MQTT...');
            mqttClient.publish('virtualpet/command', 'play');
        }
    });
});

const PORT = 3000;
server.listen(PORT, '0.0.0.0', () => {
    console.log(`🚀 Dashboard a correr em: http://localhost:${PORT}`);
    console.log(`   (Abra o link acima no seu browser)`);
});
