import hashlib
from rest_framework.decorators import api_view
from rest_framework.response import Response
from rest_framework import status
from .models import Usuario, Digital, HistoricoAcesso
from .serializers import UsuarioSerializer
from django.shortcuts import render, redirect
from django.contrib import messages
from .forms import (
    UsuarioForm, SalaForm, DigitalForm,
    UsuarioCadastroForm, DigitalFormSet
)
from .models import Usuario, Sala, Digital, HistoricoAcesso, TipoAcesso, UsuarioSala, CapturedTemplate


# ===============================
# Cadastrar usuário e digital
# ===============================
@api_view(['POST'])
def register_user(request):
    """
    Espera JSON:
    {
        "nome": "Gabriel Pican",
        "codigo": "A12345",
        "tipo_usuario": "aluno",
        "template_b64": "<string base64>",
        "dedo": 1
    }
    """
    data = request.data
    nome = data.get('nome')
    codigo = data.get('codigo')
    tipo_usuario = data.get('tipo_usuario', 'aluno')
    template_b64 = data.get('template_b64')
    dedo = data.get('dedo')

    if not nome or not codigo or not template_b64:
        return Response({'error': 'Campos obrigatórios ausentes'}, status=status.HTTP_400_BAD_REQUEST)

    # Cria ou obtém usuário
    usuario, created = Usuario.objects.get_or_create(
        codigo=codigo,
        defaults={'nome': nome, 'tipo_usuario': tipo_usuario}
    )

    # Cria a digital associada
    hash_sha256 = hashlib.sha256(template_b64.encode('utf-8')).hexdigest()
    if Digital.objects.filter(hash_sha256=hash_sha256, usuario=usuario).exists():
        return Response({'error': 'Template já cadastrado para este usuário'}, status=status.HTTP_409_CONFLICT)

    digital = Digital.objects.create(
        usuario=usuario,
        template_b64=template_b64,
        dedo=dedo
    )

    return Response({
        'message': 'Usuário e digital cadastrados com sucesso!',
        'usuario': UsuarioSerializer(usuario).data,
        'digital_id': digital.id
    }, status=status.HTTP_201_CREATED)


# ===============================
# Verificar digital enviada pelo Arduino
# ===============================
@api_view(['POST'])
def verify_template(request):
    """
    Espera JSON:
    { "template_b64": "<string base64>" }
    """
    data = request.data
    template_b64 = data.get('template_b64')

    if not template_b64:
        return Response({'error': 'Campo template_b64 obrigatório'}, status=status.HTTP_400_BAD_REQUEST)

    hash_sha256 = hashlib.sha256(template_b64.encode('utf-8')).hexdigest()

    match = Digital.objects.filter(hash_sha256=hash_sha256, ativo=True).select_related('usuario').first()

    if match:
        # Determina contexto: entrada/saida da sessão (fallback para entrada)
        tipo = request.session.get('tipo_acesso') or TipoAcesso.ENTRADA
        if tipo not in (TipoAcesso.ENTRADA, TipoAcesso.SAIDA):
            tipo = TipoAcesso.ENTRADA
        HistoricoAcesso.objects.create(
            usuario=match.usuario,
            tipo_acesso=tipo,
            motivo='Autenticacao bem-sucedida'
        )
        return Response({
            'match': True,
            'usuario': match.usuario.nome,
            'codigo': match.usuario.codigo
        }, status=status.HTTP_200_OK)

    # Caso não haja correspondência
    tipo_fail = request.session.get('tipo_acesso') or TipoAcesso.ENTRADA
    if tipo_fail not in (TipoAcesso.ENTRADA, TipoAcesso.SAIDA):
        tipo_fail = TipoAcesso.ENTRADA
    HistoricoAcesso.objects.create(
        tipo_acesso=tipo_fail,
        motivo='Falha de autenticacao',
        metadata={'hash': hash_sha256}
    )

    return Response({'match': False}, status=status.HTTP_401_UNAUTHORIZED)


# ===============================
# Remover usuário e digitais
# ===============================
@api_view(['POST'])
def remove_user(request):
    """
    Espera JSON: { "codigo": "A12345" }
    """
    codigo = request.data.get('codigo')
    if not codigo:
        return Response({'error': 'Campo codigo obrigatório'}, status=status.HTTP_400_BAD_REQUEST)

    usuario = Usuario.objects.filter(codigo=codigo).first()
    if not usuario:
        return Response({'error': 'Usuário não encontrado'}, status=status.HTTP_404_NOT_FOUND)

    usuario.delete()
    return Response({'message': f'Usuário {codigo} removido com sucesso!'}, status=status.HTTP_200_OK)


# ===============================
# Listar usuários
# ===============================
@api_view(['GET'])
def list_users(request):
    usuarios = Usuario.objects.all().order_by('id')
    serializer = UsuarioSerializer(usuarios, many=True)
    return Response(serializer.data)


def dashboard(request):
    # Página inicial: pronta para ler digitais continuamente
    # Mostra contexto (entrada/saída) e histórico recente
    ctx = request.session.get('tipo_acesso') or TipoAcesso.ENTRADA
    if ctx not in (TipoAcesso.ENTRADA, TipoAcesso.SAIDA):
        ctx = TipoAcesso.ENTRADA
    recentes = HistoricoAcesso.objects.select_related('usuario', 'sala').order_by('-data_hora')[:10]
    return render(request, 'dashboard.html', {
        'ready_message': 'Aproxime o dedo do sensor. O sistema está pronto para leitura.',
        'current_context': ctx,
        'recent_history': recentes,
    })


def register_fingerprint_page(request):
    """Fluxo único: cria usuário, associa salas e cadastra múltiplas digitais."""
    if request.method == 'POST':
        uform = UsuarioCadastroForm(request.POST, prefix='u')
        dformset = DigitalFormSet(request.POST, prefix='d')
        if uform.is_valid() and dformset.is_valid():
            usuario = uform.save()
            salas = uform.cleaned_data.get('salas')
            if salas:
                UsuarioSala.objects.bulk_create([
                    UsuarioSala(usuario=usuario, sala=s)
                    for s in salas
                ], ignore_conflicts=True)

            created = 0
            for form in dformset:
                dedo = form.cleaned_data.get('dedo')
                template = form.cleaned_data.get('template_b64')
                if not dedo or not template:
                    continue
                # Avoid duplicates for same user/template
                h = hashlib.sha256(template.encode('utf-8')).hexdigest()
                if not Digital.objects.filter(usuario=usuario, hash_sha256=h).exists():
                    Digital.objects.create(usuario=usuario, dedo=dedo, template_b64=template)
                    created += 1

            messages.success(request, f'Usuário cadastrado com {created} digitais.')
            return redirect('dashboard')
        else:
            messages.error(request, 'Verifique os dados do formulário.')
    else:
        uform = UsuarioCadastroForm(prefix='u')
        dformset = DigitalFormSet(prefix='d')

    return render(request, 'register_fingerprint.html', {
        'uform': uform,
        'dformset': dformset,
    })


def login_view(request):
    # If you later wire a form handler, change method handling here
    return render(request, 'login.html')

# ===============================
# Fluxo operador: registrar entrada/saída (opcional)
# ===============================
def set_access_context(request):
    """Define se o próximo match conta como ENTRADA ou SAÍDA (exibição ao operador)."""
    tipo = request.GET.get('tipo') or request.POST.get('tipo')
    if tipo in (TipoAcesso.ENTRADA, TipoAcesso.SAIDA):
        request.session['tipo_acesso'] = tipo
        messages.info(request, f"Contexto de acesso definido: {tipo}.")
    return redirect('dashboard')


# ===============================
# Captura: submit + latest (auxiliar para autocompletar template)
# ===============================
@api_view(['POST'])
def capture_submit(request):
    template_b64 = request.data.get('template_b64')
    if not template_b64:
        return Response({'error': 'template_b64 requerido'}, status=status.HTTP_400_BAD_REQUEST)
    CapturedTemplate.objects.create(template_b64=template_b64)
    return Response({'ok': True})


@api_view(['GET'])
def capture_latest(request):
    obj = CapturedTemplate.objects.order_by('-criado_em').first()
    if not obj:
        return Response({'template_b64': None})
    return Response({'template_b64': obj.template_b64, 'criado_em': obj.criado_em})
