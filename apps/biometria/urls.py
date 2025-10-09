from django.urls import path
from . import views

urlpatterns = [
    path('register/', views.register_user, name='register_user'),
    path('verify/', views.verify_template, name='verify_template'),
    path('remove/', views.remove_user, name='remove_user'),
    path('users/', views.list_users, name='list_users'),
]
