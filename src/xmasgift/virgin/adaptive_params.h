// Generated from the checked CynusTactics tal_params.json. Embedded defaults.
// Local extension: gleichwertig_cp replaces probabilistic equal-move selection.
// Source SHA256: 0ebaf20ca21661ad39b36769bc70fefe41b63844c7a38ed8f7c5679550cbde36
#pragma once
namespace TalParams {
inline constexpr auto suche_kandidaten = 5;
inline constexpr auto suche_mindestens_kandidaten = 3;
inline constexpr auto suche_denkzeit_ms = 2500;
inline constexpr auto suche_hash_mb = 32;
inline constexpr auto eroeffnung_beste_halbzuege = 8;
// Always randomise genuinely close choices; tactical overrides stay strongest.
inline constexpr int gleichwertig_cp = 20;
inline constexpr auto ausgleich_ziel = 0;
inline constexpr auto ausgleich_gleich_band = 30;
inline constexpr auto ausgleich_max_verlust_gegen_bester_zug = 80;
inline constexpr auto ausgleich_zufall_unter_gleichwertigen = 0.15; // Legacy source value, unused by chooser.
inline constexpr auto helfen_ab_bewertung = -150;
inline constexpr auto helfen_schritt = 80;
inline constexpr auto helfen_ziel_nahe = -60;
inline constexpr auto helfen_max_verlust_gegen_bester_zug = 150;
inline constexpr auto helfen_nie_ueber_bewertung = 30;
inline constexpr auto helfen_kein_materialverlust_ueber_cp = 100;
inline constexpr auto helfen_materialpruefung_halbzuege = 4;
inline constexpr auto helfen_nie_dame_oder_turm_opfern = true;
inline constexpr auto helfen_nur_plausible_kandidaten_bis_rang = 5;
inline constexpr auto volle_staerke_ab_bewertung = -300;
inline constexpr auto volle_staerke_immer_bei_matt_in = 12;
inline constexpr auto halten_ab_bewertung = 200;
inline constexpr auto halten_ziel = 200;
inline constexpr auto halten_klebt_bis_partieende = true;
inline constexpr auto halten_gleich_band = 40;
inline constexpr auto halten_max_verlust_gegen_bester_zug = 250;
inline constexpr auto sicherheit_max_materialverlust_in_cp = 100;
inline constexpr auto sicherheit_materialpruefung_halbzuege = 4;
inline constexpr auto sicherheit_nie_dame_oder_turm_opfern = true;
inline constexpr auto taktik_aktiv = true;
inline constexpr auto taktik_plies = 6;
inline constexpr auto taktik_min_materialgewinn = 250;
inline constexpr auto taktik_sperre_halbzuege = 4;
inline constexpr auto anpassung_aktiv = true;
inline constexpr auto anpassung_ab_halbzug = 14;
inline constexpr auto anpassung_gleich_bereich = 50;
inline constexpr auto anpassung_stabil_zuege = 3;
inline constexpr auto anpassung_min_kandidaten = 4;
inline constexpr auto anpassung_max_stufe_cp = 70;
inline constexpr auto anpassung_max_gesamtverlust_cp = 250;
inline constexpr auto anpassung_abgeben_cp = 120;
inline constexpr auto anpassung_abgeben_von = 80;
inline constexpr auto anpassung_abgeben_bis = 130;
inline constexpr auto anpassung_pause_halbzuege = 12;
inline constexpr auto anpassung_max_pro_partie = 3;
inline constexpr auto anpassung_schonfrist_halbzuege = 8;
inline constexpr auto anpassung_genauigkeit_fenster = 12;
inline constexpr auto anpassung_genauigkeit_min_zuege = 6;
inline constexpr auto anpassung_gut_bis_verlust_cp = 30;
inline constexpr auto anpassung_genauigkeit_max = 0.9;
}
