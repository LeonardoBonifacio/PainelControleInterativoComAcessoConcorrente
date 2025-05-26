# Painel de Controle Interativo com Acesso Concorrente (RP2040)

## 📋 Descrição

Este projeto implementa um **Sistema de Controle de Acesso** baseado na placa BitDogLab com processador RP2040, utilizando o sistema operacional de tempo real FreeRTOS. O sistema permite o login/logout de até 5 usuários, utilizando teclado serial, botões físicos, feedback visual através de display OLED e matriz de LEDs, além de sinalização sonora com buzzer.

---

## 🎯 O sistema gerencia:

- ✅ Entrada e saída de usuários.
- ✅ Limitação de acesso baseado em capacidade máxima.
- ✅ Interface visual dinâmica.
- ✅ Reset via botão de joystick.

---

## ⚙️ Funcionalidades

- ✅ Login e logout de usuários via teclado serial.
- ✅ Feedback visual via Display OLED SSD1306.
- ✅ Indicação do estado do sistema com LED RGB e matriz de LEDs WS2812.
- ✅ Sinalização sonora de eventos importantes com buzzer.
- ✅ Reset do sistema através de botão joystick.
- ✅ Debounce de botões com verificação por tempo.
- ✅ Sistema multitarefas com FreeRTOS, utilizando filas, semáforos e mutex para sincronização e comunicação entre tasks.

---

## 🛠️ Hardware Utilizado

- **Placa**: BitDogLab com RP2040
- **Display**: OLED SSD1306 via I2C
- **Matriz de LEDs**: WS2812, 25 LEDs
- **Buzzer**: PWM na GPIO 21
- **LEDs indicadores**:
  - Verde: GPIO 11
  - Azul: GPIO 12
  - Vermelho: GPIO 13
- **Botões**:
  - A: GPIO 5
  - B: GPIO 6
  - Joystick (Reset): GPIO 22

---

## 📚 Bibliotecas e Recursos Utilizados

- **FreeRTOS**: Sistema operacional de tempo real.
- **ssd1306**: Biblioteca para comunicação com display OLED.
- **WS2812 PIO**: Programa PIO para controle eficiente da matriz de LEDs.
- **Font**: Fonte utilizada para exibição no display.
- **Hardware SDK**: GPIO, I2C, PWM, PIO da Raspberry Pi Pico SDK.

---

## 🗂️ Estrutura do Código

### 🧩 Interrupts

- Tratamento de botões físicos com debounce.

### 🧩 Tasks

- **UART**: Recebe IDs via teclado serial.
- **Matriz de LEDs**: Exibe quantidade de usuários logados.
- **Estado com RGB**: Mostra estado de ocupação do sistema.
- **Mensagens no Display**: Mensagens de espera, login/logout, limite de usuários.
- **Aviso Limite**: Exibe aviso de capacidade máxima e toca som no buzzer.
- **Entrada/Saída de Usuários**: Gerencia ações de login e logout via botões A e B.

### 🧩 Semáforos

- Binários para sinalização de eventos de botões.
- Contador para controle de ocupação.

### 🧩 Mutex

- Exclusão mútua no acesso ao display OLED.

### 🧩 Fila

- Comunicação entre task UART e tasks de gestão de usuários.

---

## 🔄 Como Funciona

1. O sistema inicializa em uma tela de "espera por ID".
2. O usuário digita um ID pelo teclado serial.
3. O display mostra o ID informado e solicita ação via botões:
   - **A**: Fazer login.
   - **B**: Fazer logout.
4. O estado do sistema é refletido:
   - **Display**: Mensagens de ação.
   - **LEDs RGB**: Estado de ocupação.
   - **Matriz de LEDs**: Quantidade de usuários.
   - **Buzzer**: Alerta sonoro para eventos críticos.
5. Ao atingir a capacidade máxima (5 usuários), o sistema bloqueia novos logins e emite um aviso sonoro e visual.
6. O sistema pode ser resetado a qualquer momento com o botão do joystick.

---

## ⚒️ Como Compilar

1. Clone o repositório.
2. Compile utilizando o CMake com a toolchain da Raspberry Pi Pico.
3. Inclua as bibliotecas `ssd1306`, `font`, `FreeRTOS` e `ws2812.pio` conforme indicado no projeto.
4. Flash na placa BitDogLab utilizando `picotool` ou outro método preferido.

```bash
mkdir build
cd build
cmake ..
make
