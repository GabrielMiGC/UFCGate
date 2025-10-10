from django import forms
from django.forms import formset_factory
from .models import Usuario, Sala, Digital, Dedo


class UsuarioForm(forms.ModelForm):
    class Meta:
        model = Usuario
        fields = [
            'nome',
            'codigo',
            'tipo_usuario',
        ]


class SalaForm(forms.ModelForm):
    class Meta:
        model = Sala
        fields = [
            'nome',
            'descricao',
        ]


class DigitalForm(forms.ModelForm):
    class Meta:
        model = Digital
        fields = [
            'dedo',
            'template_b64',
        ]


class UsuarioCadastroForm(forms.ModelForm):
    salas = forms.ModelMultipleChoiceField(
        queryset=Sala.objects.all(),
        widget=forms.SelectMultiple(attrs={"size": 6}),
        required=False,
        help_text="Selecione as salas autorizadas"
    )

    class Meta:
        model = Usuario
        fields = [
            'nome',
            'codigo',
            'tipo_usuario',
        ]


class DigitalInlineForm(forms.Form):
    dedo = forms.ChoiceField(choices=Dedo.choices, required=True, label="Dedo")
    template_b64 = forms.CharField(
        widget=forms.Textarea(attrs={"rows": 3, "readonly": True, "placeholder": "Use o botão 'Pegar última captura'"}),
        required=True,
        label="Template (Base64)"
    )


DigitalFormSet = formset_factory(DigitalInlineForm, extra=2, max_num=10)