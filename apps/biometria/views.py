import hashlib
from rest_framework.decorators import api_view
from rest_framework.response import Response
from rest_framework import status
from .models import Usuario, Digital, HistoricoAcesso
from .serializers import UsuarioSerializer


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
        # Registra histórico de acesso (entrada)
        HistoricoAcesso.objects.create(
            usuario=match.usuario,
            tipo_acesso='entrada',
            motivo='Autenticacao bem-sucedida'
        )
        return Response({
            'match': True,
            'usuario': match.usuario.nome,
            'codigo': match.usuario.codigo
        }, status=status.HTTP_200_OK)

    # Caso não haja correspondência
    HistoricoAcesso.objects.create(
        tipo_acesso='entrada',
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
