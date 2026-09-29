const setupPanel = document.getElementById("setupPanel");
const gamePanel = document.getElementById("gamePanel");

const gameMinutesInput = document.getElementById("gameMinutesInput");
const startButton = document.getElementById("startButton");
const setupMessage = document.getElementById("setupMessage");

const gameTimeEl = document.getElementById("gameTime");
const codeSecondsEl = document.getElementById("codeSeconds");
const codeInput = document.getElementById("codeInput");
const defuseButton = document.getElementById("defuseButton");
const pauseButton = document.getElementById("pauseButton");
const resetButton = document.getElementById("resetButton");
const messageEl = document.getElementById("message");
const statusLight = document.getElementById("statusLight");

const resultOverlay = document.getElementById("resultOverlay");
const resultPopup = document.getElementById("resultPopup");
const resultTitle = document.getElementById("resultTitle");
const resultSubtitle = document.getElementById("resultSubtitle");
const popupResetButton = document.getElementById("popupResetButton");

let statusInterval = null;
let currentState = null;

function formatTime(totalSeconds) {
  const minutes = Math.floor(totalSeconds / 60);
  const seconds = totalSeconds % 60;

  return `${String(minutes).padStart(2, "0")}:${String(seconds).padStart(2, "0")}`;
}

function setMessage(text, type) {
  messageEl.textContent = text;
  messageEl.className = "message";

  if (type) {
    messageEl.classList.add(type);
  }
}

function setSetupMessage(text) {
  setupMessage.textContent = text;
}

function showSetupPanel() {
  setupPanel.classList.remove("hidden");
  gamePanel.classList.add("hidden");
}

function showGamePanel() {
  setupPanel.classList.add("hidden");
  gamePanel.classList.remove("hidden");
}

function startPolling() {
  if (!statusInterval) {
    statusInterval = setInterval(updateStatus, 1000);
  }
}

function stopPolling() {
  if (statusInterval) {
    clearInterval(statusInterval);
    statusInterval = null;
  }
}

function showResultPopup(state) {
  resultPopup.className = "result-popup";

  if (state === "defused") {
    resultPopup.classList.add("defused");
    resultTitle.textContent = "BOMBA DISINNESCATA";
    resultSubtitle.textContent = "Obiettivo completato con successo.";
  }

  if (state === "exploded") {
    resultPopup.classList.add("exploded");
    resultTitle.textContent = "BOMBA ESPLOSA";
    resultSubtitle.textContent = "Missione fallita.";
  }

  resultOverlay.classList.add("visible");
}

function hideResultPopup() {
  resultOverlay.classList.remove("visible");
  resultPopup.className = "result-popup";
}

function configurePauseButton(state) {
  if (state === "paused") {
    pauseButton.textContent = "LUCE VERDE!";
    pauseButton.classList.add("resume");
    return;
  }

  pauseButton.textContent = "GIOCO IN PAUSA";
  pauseButton.classList.remove("resume");
}

function setState(state) {
  currentState = state;

  if (state === "setup") {
    stopPolling();
    hideResultPopup();
    showSetupPanel();

    codeInput.value = "";
    codeInput.disabled = false;
    defuseButton.disabled = false;

    configurePauseButton("active");
    setMessage("", null);
    setSetupMessage("");

    return;
  }

  showGamePanel();

  statusLight.className = "status-light";
  statusLight.classList.add(state);

  configurePauseButton(state);

  if (state === "active") {
    codeInput.disabled = false;
    defuseButton.disabled = false;
    pauseButton.disabled = false;

    hideResultPopup();
    startPolling();

    if (messageEl.textContent === "GIOCO IN PAUSA") {
      setMessage("", null);
    }

    return;
  }

  if (state === "paused") {
    codeInput.disabled = true;
    defuseButton.disabled = true;
    pauseButton.disabled = false;

    setMessage("GIOCO IN PAUSA", "pause");
    stopPolling();

    return;
  }

  codeInput.disabled = true;
  defuseButton.disabled = true;
  pauseButton.disabled = true;
  stopPolling();

  if (state === "defused") {
    setMessage("BOMBA DISINNESCATA", "ok");
    showResultPopup("defused");
  }

  if (state === "exploded") {
    setMessage("BOMBA ESPLOSA", "bad");
    showResultPopup("exploded");
  }
}

async function updateStatus() {
  try {
    const response = await fetch("/api/status");
    const data = await response.json();

    gameTimeEl.textContent = formatTime(data.gameSecondsLeft);
    codeSecondsEl.textContent = data.codeSecondsLeft;

    if (data.state !== currentState) {
      setState(data.state);
    }
  } catch (error) {
    setMessage("Errore connessione backend", "bad");
  }
}

async function startGame() {
  const minutes = Number(gameMinutesInput.value);

  if (!Number.isInteger(minutes) || minutes < 1 || minutes > 180) {
    setSetupMessage("Inserisci un tempo tra 1 e 180 minuti.");
    return;
  }

  try {
    startButton.disabled = true;
    setSetupMessage("");

    const response = await fetch("/api/start", {
      method: "POST",
      headers: {
        "Content-Type": "application/json"
      },
      body: JSON.stringify({ minutes })
    });

    const data = await response.json();

    if (data.result === "started") {
      codeInput.value = "";
      setMessage("", null);
      hideResultPopup();

      gameTimeEl.textContent = formatTime(minutes * 60);
      codeSecondsEl.textContent = "45";

      setState("active");
      await updateStatus();
      startPolling();
    } else {
      setSetupMessage("Errore avvio partita.");
    }
  } catch (error) {
    setSetupMessage("Errore connessione backend.");
  } finally {
    startButton.disabled = false;
  }
}

async function togglePause() {
  if (currentState !== "active" && currentState !== "paused") {
    return;
  }

  try {
    pauseButton.disabled = true;

    const endpoint = currentState === "active" ? "/api/pause" : "/api/resume";

    await fetch(endpoint, {
      method: "POST"
    });

    await updateStatus();
  } catch (error) {
    setMessage("Errore pausa/ripresa", "bad");
  } finally {
    pauseButton.disabled = false;
  }
}

async function submitCode() {
  const code = codeInput.value.trim().toUpperCase();

  if (code.length !== 6) {
    setMessage("Inserisci un codice da 6 caratteri", "bad");
    return;
  }

  try {
    const response = await fetch("/api/submit", {
      method: "POST",
      headers: {
        "Content-Type": "application/json"
      },
      body: JSON.stringify({ code })
    });

    const data = await response.json();

    await updateStatus();

    if (data.result === "defused" || data.result === "exploded") {
      setState(data.result);
    }
  } catch (error) {
    setMessage("Errore invio codice", "bad");
  }
}

async function resetGame() {
  try {
    await fetch("/api/reset", {
      method: "POST"
    });

    codeInput.value = "";
    setMessage("", null);
    hideResultPopup();

    gameMinutesInput.value = "20";
    gameTimeEl.textContent = "20:00";
    codeSecondsEl.textContent = "45";

    setState("setup");
  } catch (error) {
    setMessage("Errore reset", "bad");
  }
}

codeInput.addEventListener("input", () => {
  codeInput.value = codeInput.value
    .toUpperCase()
    .replace(/[^A-Z0-9]/g, "")
    .slice(0, 6);
});

codeInput.addEventListener("keydown", (event) => {
  if (event.key === "Enter") {
    submitCode();
  }
});

gameMinutesInput.addEventListener("keydown", (event) => {
  if (event.key === "Enter") {
    startGame();
  }
});

startButton.addEventListener("click", startGame);
defuseButton.addEventListener("click", submitCode);
pauseButton.addEventListener("click", togglePause);
resetButton.addEventListener("click", resetGame);
popupResetButton.addEventListener("click", resetGame);

updateStatus();