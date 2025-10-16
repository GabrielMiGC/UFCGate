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
    # Observação: definimos um queryset vazio inicialmente para evitar avaliação
    # estática no momento do import do módulo. Ele será preenchido dinamicamente
    # no __init__. Isso garante que salas criadas após o start do servidor
    # (ex: 'NEMO') apareçam imediatamente no formulário sem precisar reiniciar.
    salas = forms.ModelMultipleChoiceField(
        queryset=Sala.objects.none(),
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

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        # Queryset dinâmico e ordenado alfabeticamente
        self.fields['salas'].queryset = Sala.objects.all().order_by('nome')


class DigitalInlineForm(forms.Form):
    dedo = forms.ChoiceField(choices=Dedo.choices, required=True, label="Dedo")
    template_b64 = forms.CharField(
        widget=forms.Textarea(attrs={"rows": 3, "readonly": True, "placeholder": "Use o botão 'Pegar última captura'"}),
        required=True,
        label="Template (Base64)"
    )


DigitalFormSet = formset_factory(DigitalInlineForm, extra=2, max_num=10)