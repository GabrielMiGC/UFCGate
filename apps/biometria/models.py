import hashlib
from django.db import models
from django.utils import timezone


# ============================
#  ENUMS / Choices (Sem mudanças)
# ============================

class TipoUsuario(models.TextChoices):
    ALUNO = "aluno", "Aluno"
    PROFESSOR = "professor", "Professor"


class TipoAcesso(models.TextChoices):
    ENTRADA = "entrada", "Entrada"
    SAIDA = "saida", "Saída"


class Dedo(models.TextChoices):
    INDICADOR_DIR = "indicador_dir", "Indicador DIR."
    POLEGAR_DIR = "polegar_dir", "Polegar DIR."
    MEDIO_DIR = "medio_dir", "Médio DIR."
    ANELAR_DIR = "anelar_dir", "Anelar DIR."
    MINIMO_DIR = "minimo_dir", "Mínimo DIR."
    INDICADOR_ESQ = "indicador_esq", "Indicador ESQ."
    POLEGAR_ESQ = "polegar_esq", "Polegar ESQ."
    MEDIO_ESQ = "medio_esq", "Médio ESQ."
    ANELAR_ESQ = "anelar_esq", "Anelar ESQ."
    MINIMO_ESQ = "minimo_esq", "Mínimo ESQ."


# ============================
#  MODELOS PRINCIPAIS (Sem mudanças)
# ============================

class Usuario(models.Model):
    nome = models.CharField(max_length=100)
    codigo = models.CharField(max_length=50, unique=True)
    tipo_usuario = models.CharField(
        max_length=20,
        choices=TipoUsuario.choices,
        default=TipoUsuario.ALUNO
    )
    criado_em = models.DateTimeField(default=timezone.now)

    def __str__(self):
        return f"{self.nome} ({self.codigo})"


class Sala(models.Model):
    nome = models.CharField(max_length=100, unique=True)
    descricao = models.TextField(blank=True, null=True)
    criado_em = models.DateTimeField(default=timezone.now)

    def __str__(self):
        return self.nome


class UsuarioSala(models.Model):
    usuario = models.ForeignKey(Usuario, on_delete=models.CASCADE)
    sala = models.ForeignKey(Sala, on_delete=models.CASCADE)
    criado_em = models.DateTimeField(default=timezone.now)

    class Meta:
        unique_together = ('usuario', 'sala')

    def __str__(self):
        return f"{self.usuario.nome} → {self.sala.nome}"


# ============================
#  TABELA DE DIGITAIS (MODIFICADA)
# ============================

# ========== ALTERADO PARA SUPORTE À VERSÃO A ==========
class Digital(models.Model):
    usuario = models.ForeignKey(Usuario, on_delete=models.CASCADE)
    
    # Agora opcional
    sensor_id = models.IntegerField(null=True, blank=True)

    # Template usado no fluxo 1:1
    template_b64 = models.TextField(null=True, blank=True)

    # Hash do template (opcional mas recomendado)
    hash_sha256 = models.TextField(null=True, blank=True)

    dedo = models.CharField(max_length=30, null=True, blank=True)
    ativo = models.BooleanField(default=True)
    criado_em = models.DateTimeField(auto_now_add=True)

    class Meta:
        constraints = [
            models.UniqueConstraint(
                fields=['sensor_id'],
                name='unique_sensor_id_not_null',
                condition=~models.Q(sensor_id=None)  # funciona como WHERE sensor_id IS NOT NULL
            ),
            models.UniqueConstraint(
                fields=['usuario', 'dedo'],
                name='unique_user_finger'
            )
        ]

    def __str__(self):
        return f"Digital de {self.usuario.nome} ({self.dedo})"


# ============================
#  HISTÓRICO DE ACESSOS
# ============================

class HistoricoAcesso(models.Model):
    usuario = models.ForeignKey(Usuario, on_delete=models.SET_NULL, null=True)
    sala = models.ForeignKey(Sala, on_delete=models.SET_NULL, null=True)
    data_hora = models.DateTimeField(default=timezone.now)
    tipo_acesso = models.CharField(max_length=10, choices=TipoAcesso.choices)
    motivo = models.TextField(blank=True, null=True)
    metadata = models.JSONField(blank=True, null=True) # confiança do sensor

    def __str__(self):
        u = self.usuario.nome if self.usuario else "Usuário desconhecido"
        s = self.sala.nome if self.sala else "Sala indefinida"
        return f"{u} - {s} ({self.tipo_acesso}) em {self.data_hora:%d/%m %H:%M}"