/**
 * Web Bluetooth logic for Nordic nRF52840 devices.
 * Connects to the Nordic UART Service (NUS) by default.
 */

// Nordic UART Service (NUS) UUIDs
const NUS_SERVICE_UUID = '6e400001-b5a3-f393-e0a9-e50e24dcca9e';
const NUS_RX_CHARACTERISTIC_UUID = '6e400002-b5a3-f393-e0a9-e50e24dcca9e'; // Write to device
const NUS_TX_CHARACTERISTIC_UUID = '6e400003-b5a3-f393-e0a9-e50e24dcca9e'; // Read from device / Notifications

// UI Elements
const connectBtn = document.getElementById('connectBtn');
const disconnectBtn = document.getElementById('disconnectBtn');
const connectionStatus = document.getElementById('connectionStatus');
const deviceNameEl = document.getElementById('deviceName');
const terminalOutput = document.getElementById('terminalOutput');
const clearBtn = document.getElementById('clearBtn');

// Bluetooth State
let bleDevice = null;
let bleServer = null;
let nusService = null;
let rxCharacteristic = null;
let txCharacteristic = null;

/**
 * Utility to write logs to the terminal div
 */
function logToTerminal(message, type = 'sys-msg') {
    const p = document.createElement('p');
    const timestamp = new Date().toLocaleTimeString();
    p.textContent = `[${timestamp}] ${message}`;
    p.className = type;
    terminalOutput.appendChild(p);
    terminalOutput.scrollTop = terminalOutput.scrollHeight;
}

/**
 * Connect to BLE Device
 */
async function connectDevice() {
    try {
        if (!navigator.bluetooth) {
            throw new Error('Web Bluetooth API is not available in this browser. Please use Chrome/Edge and ensure HTTPS.');
        }

        logToTerminal('Requesting Bluetooth Device...', 'sys-msg');
        
        // Request device that optionally exposes the NUS Service
        // If connecting to specific Nordic chips we search for NUS or namePrefix.
        // We can just explicitly require the NUS UUID.
        bleDevice = await navigator.bluetooth.requestDevice({
            filters: [{ services: [NUS_SERVICE_UUID] }],
            optionalServices: ['generic_access']
        });

        deviceNameEl.textContent = bleDevice.name || 'Unknown Device';
        bleDevice.addEventListener('gattserverdisconnected', onDisconnected);

        logToTerminal(`Connecting to ${bleDevice.name}...`, 'sys-msg');
        
        bleServer = await bleDevice.gatt.connect();
        
        logToTerminal('Getting Nordic UART Service...', 'sys-msg');
        nusService = await bleServer.getPrimaryService(NUS_SERVICE_UUID);

        logToTerminal('Getting RX/TX Characteristics...', 'sys-msg');
        txCharacteristic = await nusService.getCharacteristic(NUS_TX_CHARACTERISTIC_UUID);
        rxCharacteristic = await nusService.getCharacteristic(NUS_RX_CHARACTERISTIC_UUID);

        // Subscribe to notifications from the TX characteristic (Device sending to Web App)
        await txCharacteristic.startNotifications();
        txCharacteristic.addEventListener('characteristicvaluechanged', handleNotifications);

        updateUIStatus(true);
        logToTerminal('Connected successfully. Listening for data...', 'sys-msg');

    } catch (error) {
        logToTerminal(`Connection failed: ${error.message}`, 'err-msg');
        console.error(error);
        updateUIStatus(false);
    }
}

/**
 * Disconnect from BLE Device
 */
function disconnectDevice() {
    if (!bleDevice) { return; }
    
    logToTerminal('Disconnecting...', 'sys-msg');
    if (bleDevice.gatt.connected) {
        bleDevice.gatt.disconnect();
    } else {
        logToTerminal('Bluetooth Device is already disconnected', 'sys-msg');
    }
}

/**
 * Disconnect Event Handler
 */
function onDisconnected(event) {
    const device = event.target;
    logToTerminal(`Device ${device.name} is disconnected.`, 'sys-msg');
    updateUIStatus(false);
    
    bleDevice = null;
    bleServer = null;
    nusService = null;
    rxCharacteristic = null;
    txCharacteristic = null;
}

/**
 * Handle incoming data from nRF52840
 */
function handleNotifications(event) {
    const value = event.target.value;
    const decoder = new TextDecoder('utf-8');
    const textBase = decoder.decode(value);
    
    logToTerminal(`RX: ${textBase}`, 'rx-msg');
}

/**
 * Send data to nRF52840 (for future use if needed)
 */
async function sendData(dataString) {
    if (!rxCharacteristic) {
        logToTerminal('Must connect to a device before sending data.', 'err-msg');
        return;
    }
    
    try {
        const encoder = new TextEncoder();
        const data = encoder.encode(dataString + '\n');
        await rxCharacteristic.writeValue(data);
        logToTerminal(`TX: ${dataString}`, 'tx-msg');
    } catch (error) {
        logToTerminal(`Send failed: ${error.message}`, 'err-msg');
    }
}

/**
 * Update UI Elements based on connection state
 */
function updateUIStatus(isConnected) {
    connectBtn.disabled = isConnected;
    disconnectBtn.disabled = !isConnected;
    
    if (isConnected) {
        connectionStatus.textContent = 'Connected';
        connectionStatus.className = 'status-connected';
    } else {
        connectionStatus.textContent = 'Disconnected';
        connectionStatus.className = 'status-disconnected';
        deviceNameEl.textContent = '-';
    }
}

// Event Listeners
connectBtn.addEventListener('click', connectDevice);
disconnectBtn.addEventListener('click', disconnectDevice);

clearBtn.addEventListener('click', () => {
    terminalOutput.innerHTML = '';
});
