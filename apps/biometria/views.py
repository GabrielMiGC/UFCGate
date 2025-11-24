import requests
import os
from rest_framework.decorators import api_view
from rest_framework.response import Response
from rest_framework import status
from django.shortcuts import render, redirect
from django.contrib import messages
from django.conf import settings
from django.contrib.admin.views.decorators import staff_member_required

from .models import (
    Usuario, Digital, HistoricoAcesso, TipoAcesso, UsuarioSala, Sala
)
from .forms import (
    UsuarioCadastroForm # Vamos manter este, mas simplificado
)

# ===============================
# Helper: Função para falar com o Bridge
# ===============================

def send_bridge_command(command: str):
    """
    Envia um comando (ex: "ENROLL:5") para a API do serial_bridge.py
    """
    bridge_url = os.getenv('BRIDGE_API_URL')
    if not bridge_url:
        print("ERRO: BRIDGE_API_URL não está definida no .env")
        return False, "Bridge API URL não configurada"
        
    try:
        response = requests.post(
            bridge_url,
            json={'command': command},
            timeout=5 # Timeout de 5 segundos
        )
        response.raise_for_status() # Lança erro se for 4xx/5xx
        return True, response.json()
    except requests.exceptions.RequestException as e:
        print(f"ERRO ao conectar com o bridge: {e}")
        return False, str(e)

# ===============================
# API: Bridge -> Django (Log de Acesso)
# ===============================
@api_view(['POST'])
def log_access(request):
    """
    Recebe um SENSOR_ID do bridge, valida e registra o acesso.
    JSON esperado: { "sensor_id": 5, "confidence": 95 }
    """
    sensor_id = request.data.get('sensor_id')
    confidence = request.data.get('confidence')
    
    if not sensor_id:
        return Response({'error': 'sensor_id obrigatório'}, status=status.HTTP_400_BAD_REQUEST)

    digital = Digital.objects.filter(sensor_id=sensor_id, ativo=True).select_related('usuario').first()

    if not digital:
        # Digital não encontrada ou inativa
        HistoricoAcesso.objects.create(
            tipo_acesso=TipoAcesso.ENTRADA, # Fallback
            motivo=f"Falha de autenticacao: sensor_id {sensor_id} desconhecido.",
            metadata={'sensor_id': sensor_id, 'confidence': confidence}
        )
        return Response({'error': 'Digital não cadastrada'}, status=status.HTTP_404_NOT_FOUND)

    # --- Match Encontrado ---
    usuario = digital.usuario
    
    # Determina contexto (entrada/saida)
    tipo = request.session.get('tipo_acesso') or TipoAcesso.ENTRADA
    if tipo not in (TipoAcesso.ENTRADA, TipoAcesso.SAIDA):
        tipo = TipoAcesso.ENTRADA

    # *** LÓGICA DE PERMISSÃO ***
    # TODO: Implementar lógica de qual sala o sensor está (por enquanto, log genérico)
    # Por agora, apenas registramos que o *usuário* foi visto.
    
    HistoricoAcesso.objects.create(
        usuario=usuario,
        tipo_acesso=tipo,
        motivo=f"Acesso por {digital.get_dedo_display()}",
        metadata={'sensor_id': sensor_id, 'confidence': confidence}
    )
    
    return Response({
        'match': True,
        'usuario': usuario.nome,
        'codigo': usuario.codigo
    }, status=status.HTTP_200_OK)


# ===============================
# API: Admin -> Bridge (Comandos)
# ===============================

@staff_member_required # Garante que só o admin chame
@api_view(['POST'])
def sensor_enroll_command(request):
    """
    API interna para o Admin enviar um comando de CADASTRO.
    JSON esperado: { "sensor_id": 5 }
    """
    sensor_id = request.data.get('sensor_id')
    if not sensor_id:
        return Response({'error': 'sensor_id obrigatório'}, status=status.HTTP_400_BAD_REQUEST)
        
    ok, response_data = send_bridge_command(f"ENROLL:{sensor_id}")
    
    if ok:
        return Response({'status': 'Comando ENROLL enviado', 'bridge_response': response_data})
    else:
        return Response({'error': f'Falha ao enviar comando: {response_data}'}, status=status.HTTP_500_INTERNAL_SERVER_ERROR)

@staff_member_required
@api_view(['POST'])
def sensor_delete_command(request):
    """
    API interna para o Admin enviar um comando de DELETE.
    JSON esperado: { "sensor_id": 5 }
    """
    sensor_id = request.data.get('sensor_id')
    if not sensor_id:
        return Response({'error': 'sensor_id obrigatório'}, status=status.HTTP_400_BAD_REQUEST)

    ok, response_data = send_bridge_command(f"DELETE:{sensor_id}")
    
    if ok:
        return Response({'status': 'Comando DELETE enviado', 'bridge_response': response_data})
    else:
        return Response({'error': f'Falha ao enviar comando: {response_data}'}, status=status.HTTP_500_INTERNAL_SERVER_ERROR)


# ===============================
# Views de UI (Páginas)
# ===============================

def dashboard(request):
    """
    Dashboard principal. Removemos a "Consulta Manual" que é obsoleta.
    """
    ctx = request.session.get('tipo_acesso') or TipoAcesso.ENTRADA
    if ctx not in (TipoAcesso.ENTRADA, TipoAcesso.SAIDA):
        ctx = TipoAcesso.ENTRADA
    recentes = HistoricoAcesso.objects.select_related('usuario', 'sala').order_by('-data_hora')[:10]
    return render(request, 'dashboard.html', {
        'current_context': ctx,
        'recent_history': recentes,
    })

def set_access_context(request):
    """Define se o próximo match conta como ENTRADA ou SAÍDA."""
    tipo = request.GET.get('tipo') or request.POST.get('tipo')
    if tipo in (TipoAcesso.ENTRADA, TipoAcesso.SAIDA):
        request.session['tipo_acesso'] = tipo
        messages.info(request, f"Contexto de acesso definido: {tipo}.")
    return redirect('dashboard')


def login_view(request):
    return render(request, 'login.html')


@api_view(['POST'])
def registrar_template(request):
    """
    Recebe o template enviado pelo bridge (versão 1:1)
    """
    usuario_id = request.data.get('usuario_id')
    template_b64 = request.data.get('template_b64')
    dedo = request.data.get('dedo')

    if not usuario_id or not template_b64:
        return Response({"erro": "Dados incompletos"}, status=400)

    digital, _ = Digital.objects.update_or_create(
        usuario_id=usuario_id,
        dedo=dedo,
        defaults={
            'template_b64': template_b64,
            'ativo': True,
        }
    )

    return Response({"status": "ok", "digital_id": digital.id})

@api_view(['GET'])
def listar_templates(request):
    digit = Digital.objects.filter(ativo=True)

    lista = [
        {
            "id": d.id,
            "usuario_id": d.usuario_id,
            "template_b64": d.template_b64,
            "dedo": d.dedo
        }
        for d in digit
    ]

    return Response(lista)


# ===============================
# PÁGINAS E VIEWS OBSOLETAS
# ===============================
# register_user -> Removido (agora é 'log_access')
# verify_template -> Removido (agora é 'log_access')
# remove_user -> Removido (será feito pelo Admin)
# list_users -> Removido (será feito pelo Admin)
# register_fingerprint_page -> Removido (Admin faz isso)
# capture_submit -> Removido (não precisamos mais)
# capture_latest -> Removido (não precisamos mais)
# consult_fingerprint -> Removido (não precisamos mais)
# confirm_access -> Removido (não precisamos mais)