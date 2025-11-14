from django.urls import path
from . import views

urlpatterns = [
    # Endpoint que o BRIDGE chama quando o SENSOR lê uma digital
    path('log_access/', views.log_access, name='log_access'),

    # Endpoint para o ADMIN mandar o bridge cadastrar
    path('sensor/enroll/', views.sensor_enroll_command, name='sensor_enroll'),
    # Endpoint para o ADMIN mandar o bridge deletar
    path('sensor/delete/', views.sensor_delete_command, name='sensor_delete'),
]