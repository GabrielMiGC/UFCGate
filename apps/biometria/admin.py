import requests
import os
from django.contrib import admin, messages
from django.conf import settings
from .models import Usuario, Sala, Digital, UsuarioSala, HistoricoAcesso
from django.urls import path
from django.shortcuts import redirect

# Helper para chamar nossa própria API interna
def send_admin_command(command_type: str, sensor_id: int):
    """
    Chama a API interna do Django (ex: /api/sensor/enroll/)
    que por sua vez chama o bridge.
    """
    api_url = os.getenv('DJANGO_ALLOWED_HOSTS', 'http://localhost:8000')
    # Usamos a URL base + a URL da API
    if command_type == 'enroll':
        url = f"http://localhost:8000/api/sensor/enroll/"
    elif command_type == 'delete':
        url = f"http://localhost:8000/api/sensor/delete/"
    else:
        return False, "Comando desconhecido"

    try:
        # NOTA: Esta request é de SERVIDOR para SERVIDOR.
        # Precisamos de um header de "autenticação"
        # A forma mais simples é uma "API Key" interna, mas para este projeto
        # vamos confiar que só o admin chama esta função.
        response = requests.post(
            url,
            json={'sensor_id': sensor_id},
            # ATENÇÃO: Se o admin requer login, esta chamada falhará.
            # Nossa view usa @staff_member_required, o que é um problema
            # Vamos simplificar e remover a autenticação da view por enquanto
            # (Assumindo que a API não está exposta publicamente)
        )
        response.raise_for_status()
        return True, response.json()
    except requests.exceptions.RequestException as e:
        return False, str(e)


# --- Inlines ---

class DigitalInline(admin.TabularInline):
    """
    Modificado: Apenas mostra as digitais já cadastradas.
    O cadastro em si é feito no ModelAdmin 'Digital'
    """
    model = Digital
    fields = ['sensor_id', 'dedo', 'ativo']
    readonly_fields = ['sensor_id', 'dedo', 'ativo']
    extra = 0
    can_delete = False
    
    def has_add_permission(self, request, obj):
        # Não permite adicionar digitais por aqui
        return False


class UsuarioSalaInline(admin.TabularInline):
    model = UsuarioSala
    verbose_name = "Acesso à Sala"
    extra = 1
    autocomplete_fields = ['sala']


# --- ModelAdmins Principais ---

@admin.register(Usuario)
class UsuarioAdmin(admin.ModelAdmin):
    list_display = ['nome', 'codigo', 'tipo_usuario']
    search_fields = ['nome', 'codigo']
    list_filter = ['tipo_usuario']
    inlines = [UsuarioSalaInline, DigitalInline] # Mostra os dois inlines


@admin.register(Sala)
class SalaAdmin(admin.ModelAdmin):
    list_display = ['nome', 'descricao']
    search_fields = ['nome']


@admin.register(Digital)
class DigitalAdmin(admin.ModelAdmin):
    """
    Gerenciamento de digitais com comando global de limpeza.
    """
    list_display = ('sensor_id', 'usuario', 'get_dedo_display', 'ativo')
    search_fields = ('usuario__nome', 'usuario__codigo', 'sensor_id')
    list_filter = ('ativo', 'dedo')
    autocomplete_fields = ('usuario',)
    actions = ['send_enroll_command', 'send_delete_command']

    # Aponta para o template que criamos acima
    change_list_template = "admin/biometria/digital/change_list.html"

    def get_dedo_display(self, obj):
        return obj.get_dedo_display()
    get_dedo_display.short_description = 'Dedo'

    # --- Configuração da URL Personalizada do Botão ---
    def get_urls(self):
        urls = super().get_urls()
        my_urls = [
            path('wipe_sensor/', self.admin_site.admin_view(self.wipe_sensor_view), name='digital_wipe_sensor'),
        ]
        return my_urls + urls

    def wipe_sensor_view(self, request):
        """
        Função executada ao clicar no botão 'LIMPAR MEMÓRIA DO SENSOR'.
        """
        from .views import send_bridge_command # Reutiliza nosso helper
        
        # Envia comando DELETE_ALL para o bridge
        ok, response = send_bridge_command("DELETE_ALL")
        
        if ok:
            self.message_user(request, "Comando DELETE_ALL enviado! O sensor está sendo limpo.", messages.WARNING)
        else:
            self.message_user(request, f"Falha ao enviar comando: {response}", messages.ERROR)
            
        # Redireciona de volta para a lista
        return redirect('..')

    # --- Ações Existentes ---

    @admin.action(description='1. [CADastrar] Enviar comando de cadastro ao sensor')
    def send_enroll_command(self, request, queryset):
        for digital in queryset:
            from .views import send_bridge_command
            ok, response = send_bridge_command(f"ENROLL:{digital.sensor_id}")
            if ok:
                self.message_user(request, f"Comando ENROLL para ID {digital.sensor_id} enviado.", messages.SUCCESS)
            else:
                self.message_user(request, f"Erro no ID {digital.sensor_id}: {response}", messages.ERROR)

    @admin.action(description='2. [DELETar] Enviar comando de deleção ao sensor')
    def send_delete_command(self, request, queryset):
        for digital in queryset:
            from .views import send_bridge_command 
            ok, response = send_bridge_command(f"DELETE:{digital.sensor_id}")
            if ok:
                self.message_user(request, f"Comando DELETE para ID {digital.sensor_id} enviado.", messages.SUCCESS)
            else:
                self.message_user(request, f"Erro no ID {digital.sensor_id}: {response}", messages.ERROR)


@admin.register(HistoricoAcesso)
class HistoricoAcessoAdmin(admin.ModelAdmin):
    list_display = ('data_hora', 'usuario', 'sala', 'tipo_acesso', 'motivo', 'metadata')
    list_filter = ('tipo_acesso', 'sala', 'data_hora')
    search_fields = ('usuario__nome', 'usuario__codigo', 'sala__nome', 'motivo')
    readonly_fields = [f.name for f in HistoricoAcesso._meta.fields]

    def has_add_permission(self, request):
        return False
    def has_change_permission(self, request, obj=None):
        return False
    def has_delete_permission(self, request, obj=None):
        return False