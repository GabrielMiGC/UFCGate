from django.urls import path
from . import views

urlpatterns = [
    # API endpoints (your existing ones)
    path('register/', views.register_user, name='register_user'),
    path('verify/', views.verify_template, name='verify_template'),
    path('remove/', views.remove_user, name='remove_user'),
    path('users/', views.list_users, name='list_users'),
    path('capture/submit/', views.capture_submit, name='capture_submit'),
    path('capture/latest/', views.capture_latest, name='capture_latest'),
    path('consult/fingerprint/', views.consult_fingerprint, name='consult_fingerprint'),
    path('confirm/access/', views.confirm_access, name='confirm_access'),
]