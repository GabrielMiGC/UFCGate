# Dockerfile (para desenvolvimento)
FROM python:3.11-slim

# dependências do sistema (ajuste conforme necessário)
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    libpq-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copia requirements.txt (se tiver)
COPY requirements.txt /app/requirements.txt
RUN pip install --upgrade pip
RUN pip install -r /app/requirements.txt

# Copia o resto do código (em dev, docker-compose também monta o volume)
COPY . /app

# Expõe a porta
EXPOSE 8000

# default command is set by docker-compose (migrate + runserver)
