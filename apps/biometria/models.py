import hashlib
from django.db import models
from django.utils import timezone


# ============================
#  ENUMS / Choices
# ============================

class TipoUsuario(models.TextChoices):
    ALUNO = "aluno", "Aluno"
    PROFESSOR = "professor", "Professor"


class TipoAcesso(models.TextChoices):
    ENTRADA = "entrada", "Entrada"
    SAIDA = "saida", "Saída"


# ============================
#  MODELOS PRINCIPAIS
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
#  TABELA DE DIGITAIS
# ============================

class Digital(models.Model):
    usuario = models.ForeignKey(Usuario, on_delete=models.CASCADE, related_name="digitais")
    template_b64 = models.TextField()  # Base64 do template biométrico
    hash_sha256 = models.CharField(max_length=64, editable=False)
    dedo = models.PositiveSmallIntegerField(blank=True, null=True)
    ativo = models.BooleanField(default=True)
    criado_em = models.DateTimeField(default=timezone.now)

    class Meta:
        unique_together = ('usuario', 'hash_sha256')

    def save(self, *args, **kwargs):
        # Gera hash SHA-256 a partir do template Base64
        if self.template_b64:
            self.hash_sha256 = hashlib.sha256(self.template_b64.encode('utf-8')).hexdigest()
        super().save(*args, **kwargs)

    def __str__(self):
        dedo_str = f"Dedo {self.dedo}" if self.dedo else "Digital"
        return f"{dedo_str} de {self.usuario.nome}"


# ============================
#  HISTÓRICO DE ACESSOS
# ============================

class HistoricoAcesso(models.Model):
    usuario = models.ForeignKey(Usuario, on_delete=models.SET_NULL, null=True)
    sala = models.ForeignKey(Sala, on_delete=models.SET_NULL, null=True)
    data_hora = models.DateTimeField(default=timezone.now)
    tipo_acesso = models.CharField(max_length=10, choices=TipoAcesso.choices)
    motivo = models.TextField(blank=True, null=True)
    metadata = models.JSONField(blank=True, null=True)

    def __str__(self):
        u = self.usuario.nome if self.usuario else "Usuário desconhecido"
        s = self.sala.nome if self.sala else "Sala indefinida"
        return f"{u} - {s} ({self.tipo_acesso}) em {self.data_hora:%d/%m %H:%M}"
