from django.urls import path
from . import views

urlpatterns = [
    path('competitions/', views.CompetitionListView.as_view(), name='competition_list'),
    path('competition-starts/', views.CompetitionStartListView.as_view(), name='competition_start_list'),
    path('active-competitor/', views.ActiveCompetitionStartView.as_view(), name='active_competitor'),
    path('reorder-competitors/', views.ReorderCompetitorsView.as_view(), name='reorder_competitors'),
    path('public-display/', views.PublicDisplayView.as_view(), name='public_display'),
    path('reconcile-signals/', views.ReconcileSignalsView.as_view(), name='reconcile_signals'),
    path('pause-resume/', views.PauseResumeTimerView.as_view(), name='pause_resume_timer'),
    path('manual-timing/', views.ManualTimingView.as_view(), name='manual_timing'),
]
