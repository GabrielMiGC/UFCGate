# serial_bridge — README (pt-BR)

Descrição
- O `serial_bridge.py` é uma ponte entre uma porta serial (ex.: Arduino, ESP) e a sua aplicação. Ele lê dados da porta serial e os encaminha conforme implementado no próprio script (por exemplo: exibe no stdout, envia para uma API HTTP, publica via WebSocket, etc.). Verifique `serial_bridge.py` para o comportamento exato.

Pré-requisitos
- Python 3.8+ instalado.
- Permissão de acesso à porta serial:
  - Windows: porta nomeada como `COMx`.
  - Linux: dispositivo em `/dev/ttyUSB0`, `/dev/ttyACM0` etc. Adicione o usuário ao grupo `dialout` (sudo usermod -a -G dialout $USER) ou execute com sudo.
- Pip disponível para instalar dependências.
- Arquivo de dependências: `requirements-bridge.txt` (na mesma pasta).

Instalação (recomendado: ambiente virtual)
- Windows (PowerShell):
  pwsh:
  ```
  python -m venv .venv
  .venv\Scripts\Activate.ps1
  python -m pip install --upgrade pip
  pip install -r requirements-bridge.txt
  ```
- Linux / macOS:
  ```
  python3 -m venv .venv
  source .venv/bin/activate
  python -m pip install --upgrade pip
  pip install -r requirements-bridge.txt
  ```

Execução
- Primeiro, confira as opções do script:
  ```
  python serial_bridge.py -h
  ```
  (ou `--help`) — isso mostra os argumentos suportados (porta, baudrate, endpoint, etc.).
- Exemplos comuns:
  - Windows (COM3, 9600 baud):
    ```
    python serial_bridge.py --port COM3 --baud 9600
    ```
  - Linux (/dev/ttyUSB0, 115200 baud):
    ```
    python serial_bridge.py --port /dev/ttyUSB0 --baud 115200
    ```
- Executar em background:
  - Linux (nohup):
    ```
    nohup python serial_bridge.py --port /dev/ttyUSB0 --baud 115200 > bridge.log 2>&1 &
    ```
  - Windows (PowerShell Start-Process):
    ```
    Start-Process -FilePath python -ArgumentList "serial_bridge.py --port COM3 --baud 9600" -WindowStyle Hidden
    ```

Serviço systemd (exemplo Linux)
- Criar arquivo `/etc/systemd/system/serial-bridge.service`:
  ```
  [Unit]
  Description=Serial Bridge
  After=network.target

  [Service]
  User=seu_usuario
  WorkingDirectory=/caminho/para/UFCGate/bridge
  ExecStart=/caminho/para/.venv/bin/python /caminho/para/UFCGate/bridge/serial_bridge.py --port /dev/ttyUSB0 --baud 115200
  Restart=always

  [Install]
  WantedBy=multi-user.target
  ```
- Em seguida:
  ```
  sudo systemctl daemon-reload
  sudo systemctl enable --now serial-bridge
  sudo journalctl -u serial-bridge -f
  ```

Solução de problemas
- Erro de permissão: garanta que o usuário tem acesso à porta serial ou execute com sudo.
- Porta inválida: verifique em Device Manager (Windows) ou `dmesg | grep tty` (Linux) qual dispositivo foi criado.
- Dependências faltando: execute `pip install -r requirements-bridge.txt`.
- Para entender exatamente o fluxo de dados (para onde os dados lidos são enviados), abra `serial_bridge.py` e procure por chamadas a `requests`, `websocket`, `socket` ou por escrita em stdout/log.

Notas finais
- Este README dá instruções gerais. Para instruções 100% precisas (argumentos suportados e destino dos dados), verifique a documentação interna do repositório ou examine o cabeçalho e o bloco `if __name__ == "__main__":` em `serial_bridge.py`.
- Posso ajustar este README depois de ver o conteúdo de `serial_bridge.py` e listar opções/flags reais usadas pelo script.
```// filepath: c:\Users\gabri\OneDrive\Documents\testes de codigos\UFCGate\bridge\README.md

# serial_bridge — README (pt-BR)

Descrição
- O `serial_bridge.py` é uma ponte entre uma porta serial (ex.: Arduino, ESP) e a sua aplicação. Ele lê dados da porta serial e os encaminha conforme implementado no próprio script (por exemplo: exibe no stdout, envia para uma API HTTP, publica via WebSocket, etc.). Verifique `serial_bridge.py` para o comportamento exato.

Pré-requisitos
- Python 3.8+ instalado.
- Permissão de acesso à porta serial:
  - Windows: porta nomeada como `COMx`.
  - Linux: dispositivo em `/dev/ttyUSB0`, `/dev/ttyACM0` etc. Adicione o usuário ao grupo `dialout` (sudo usermod -a -G dialout $USER) ou execute com sudo.
- Pip disponível para instalar dependências.
- Arquivo de dependências: `requirements-bridge.txt` (na mesma pasta).

Instalação (recomendado: ambiente virtual)
- Windows (PowerShell):
  pwsh:
  ```
  python -m venv .venv
  .venv\Scripts\Activate.ps1
  python -m pip install --upgrade pip
  pip install -r requirements-bridge.txt
  ```
- Linux / macOS:
  ```
  python3 -m venv .venv
  source .venv/bin/activate
  python -m pip install --upgrade pip
  pip install -r requirements-bridge.txt
  ```

Execução
- Primeiro, confira as opções do script:
  ```
  python serial_bridge.py -h
  ```
  (ou `--help`) — isso mostra os argumentos suportados (porta, baudrate, endpoint, etc.).
- Exemplos comuns:
  - Windows (COM3, 9600 baud):
    ```
    python serial_bridge.py --port COM3 --baud 9600
    ```
  - Linux (/dev/ttyUSB0, 115200 baud):
    ```
    python serial_bridge.py --port /dev/ttyUSB0 --baud 115200
    ```
- Executar em background:
  - Linux (nohup):
    ```
    nohup python serial_bridge.py --port /dev/ttyUSB0 --baud 115200 > bridge.log 2>&1 &
    ```
  - Windows (PowerShell Start-Process):
    ```
    Start-Process -FilePath python -ArgumentList "serial_bridge.py --port COM3 --baud 9600" -WindowStyle Hidden
    ```

Serviço systemd (exemplo Linux)
- Criar arquivo `/etc/systemd/system/serial-bridge.service`:
  ```
  [Unit]
  Description=Serial Bridge
  After=network.target

  [Service]
  User=seu_usuario
  WorkingDirectory=/caminho/para/UFCGate/bridge
  ExecStart=/caminho/para/.venv/bin/python /caminho/para/UFCGate/bridge/serial_bridge.py --port /dev/ttyUSB0 --baud 115200
  Restart=always

  [Install]
  WantedBy=multi-user.target
  ```
- Em seguida:
  ```
  sudo systemctl daemon-reload
  sudo systemctl enable --now serial-bridge
  sudo journalctl -u serial-bridge -f
  ```

Solução de problemas