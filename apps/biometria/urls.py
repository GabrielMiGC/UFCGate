from django.urls import path
from . import views

urlpatterns = [
    # UI routes expected by templates
    path('', views.dashboard, name='dashboard'),
    path('register_fingerprint/', views.register_fingerprint_page, name='register_fingerprint'),
    path('login/', views.login_view, name='login'),
    path('set_access_context/', views.set_access_context, name='set_access_context'),
]
