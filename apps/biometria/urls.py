from django.urls import path
from . import views

urlpatterns = [
    # UI routes expected by templates
    path('', views.dashboard, name='dashboard'),
    path('register_fingerprint/', views.register_fingerprint_page, name='register_fingerprint'),
    path('login/', views.login_view, name='login'),
]
