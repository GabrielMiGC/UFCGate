from rest_framework import serializers
from .models import Usuario, Sala, UsuarioSala, Digital, HistoricoAcesso


class UsuarioSerializer(serializers.ModelSerializer):
    class Meta:
        model = Usuario
        fields = ['id', 'nome', 'codigo', 'tipo_usuario', 'criado_em']


class SalaSerializer(serializers.ModelSerializer):
    class Meta:
        model = Sala
        fields = ['id', 'nome', 'descricao', 'criado_em']


class UsuarioSalaSerializer(serializers.ModelSerializer):
    usuario = UsuarioSerializer(read_only=True)
    sala = SalaSerializer(read_only=True)

    class Meta:
        model = UsuarioSala
        fields = ['id', 'usuario', 'sala', 'criado_em']


class DigitalSerializer(serializers.ModelSerializer):
    usuario = UsuarioSerializer(read_only=True)

    class Meta:
        model = Digital
        fields = [
            'id',
            'usuario',
            'template_b64',
            'hash_sha256',
            'sensor_id',
            'dedo',
            'ativo',
            'criado_em'
        ]
        read_only_fields = ['hash_sha256', 'criado_em']
