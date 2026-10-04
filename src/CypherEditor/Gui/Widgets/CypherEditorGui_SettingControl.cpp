//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_SettingControl.cpp
//  Purpose: Implements controls bound to one registered setting.
//
//  History:
//  - Created by Karlo Siric on 2026-09-30
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditorGui_SettingControl.h"

#include "CypherCommon/Tier0/CypherCommon_Assert.h"
#include "CypherCommon/Tier0/CypherCommon_Log.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QLocale>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QWidget>

#include <algorithm>
#include <climits>

namespace cypher::editor::gui
{

using namespace cypher::common;

namespace
{

QString FromView( string_view_t view )
{
    return QString::fromUtf8( view.pData != nullptr ? view.pData : "", static_cast<qsizetype>( view.cchLength ) );
}

// Reals read as files write them ("0.25", a dot in any locale).
class c_real_spin_t final : public QDoubleSpinBox {
public:
    explicit c_real_spin_t( QWidget *pParent ) : QDoubleSpinBox( pParent )
    {
        setLocale( QLocale::c() );
        setDecimals( 4 );
    }

protected:
    QString textFromValue( double value ) const override { return QLocale::c().toString( value, 'g', 10 ); }
};

class setting_control_t final : public QWidget {
public:
    setting_control_t( QWidget *pParent, settings_registry_t *pRegistry, const setting_descriptor_t &descriptor )
        : QWidget( pParent ), m_pRegistry( pRegistry ), m_pDescriptor( &descriptor )
    {
        setObjectName( QString::fromUtf8( descriptor.pPath ) );
        setToolTip( QStringLiteral( "%1\n%2" ).arg( QString::fromUtf8( descriptor.pDescription != nullptr ? descriptor.pDescription : "" ),
                                                    QString::fromUtf8( descriptor.pPath ) ) );
        auto *pLayout = new QHBoxLayout( this );
        pLayout->setContentsMargins( 0, 0, 0, 0 );
        m_pEditor = CreateEditor();
        pLayout->addWidget( m_pEditor );
        ( void )EditorSettings_AddListener( pRegistry, &setting_control_t::OnChanged, this );
        Refresh();
    }

    ~setting_control_t() override { EditorSettings_RemoveListener( m_pRegistry, &setting_control_t::OnChanged, this ); }

private:
    static void OnChanged( void *pContext, string_view_t path ) noexcept
    {
        auto *pControl = static_cast<setting_control_t *>( pContext );
        if ( path.cchLength == 0u || StringView_Equals( path, StringView_FromCString( pControl->m_pDescriptor->pPath ) ) ) { pControl->Refresh(); }
    }

    void Commit( const setting_value_t &value )
    {
        if ( m_bRefreshing ) { return; }
        if ( EditorSettings_Write( m_pRegistry, settings_scope_t::USER, *m_pDescriptor, value ) != settings_registry_status_t::OK ) {
            CY_LOG_WRITE( Warning, Editor, "Setting was not written; no user scope to save it to" );
            Refresh();
        }
    }

    QWidget *CreateEditor()
    {
        const setting_descriptor_t &descriptor = *m_pDescriptor;
        switch ( descriptor.type ) {
            case setting_type_t::BOOL: {
                auto *pBox = new QCheckBox( this );
                QObject::connect( pBox, &QCheckBox::toggled, this, [this]( bool bChecked ) {
                    setting_value_t value{};
                    value.type = setting_type_t::BOOL;
                    value.bValue = bChecked;
                    Commit( value );
                } );
                return pBox;
            }
            case setting_type_t::INTEGER: {
                auto *pSpin = new QSpinBox( this );
                pSpin->setRange( static_cast<int>( std::max<i64>( descriptor.nMin, INT_MIN ) ), static_cast<int>( std::min<i64>( descriptor.nMax, INT_MAX ) ) );
                pSpin->setKeyboardTracking( false );
                QObject::connect( pSpin, &QSpinBox::valueChanged, this, [this]( int nValue ) {
                    setting_value_t value{};
                    value.type = setting_type_t::INTEGER;
                    value.nValue = nValue;
                    Commit( value );
                } );
                return pSpin;
            }
            case setting_type_t::REAL: {
                auto *pSpin = new c_real_spin_t( this );
                pSpin->setRange( std::max( descriptor.flMin, -1.0e12 ), std::min( descriptor.flMax, 1.0e12 ) );
                pSpin->setKeyboardTracking( false );
                QObject::connect( pSpin, &QDoubleSpinBox::valueChanged, this, [this]( double flValue ) {
                    setting_value_t value{};
                    value.type = setting_type_t::REAL;
                    value.flValue = flValue;
                    Commit( value );
                } );
                return pSpin;
            }
            case setting_type_t::ENUM: {
                auto *pCombo = new QComboBox( this );
                for ( usize i = 0u; i < descriptor.nEnumValues; ++i ) { pCombo->addItem( QString::fromUtf8( descriptor.ppEnumValues[i] ) ); }
                QObject::connect( pCombo, &QComboBox::currentTextChanged, this, [this]( const QString &text ) {
                    const QByteArray utf8 = text.toUtf8();
                    setting_value_t value{};
                    value.type = setting_type_t::ENUM;
                    value.text = string_view_t{ utf8.constData(), static_cast<usize>( utf8.size() ) };
                    Commit( value );
                } );
                return pCombo;
            }
            case setting_type_t::STRING:
            case setting_type_t::COLOR: {
                // Colours are rare in tool options; they edit as "#rrggbb" text.
                auto *pEdit = new QLineEdit( this );
                QObject::connect( pEdit, &QLineEdit::editingFinished, this, [this, pEdit]() {
                    const QByteArray utf8 = pEdit->text().toUtf8();
                    setting_value_t value{};
                    value.type = m_pDescriptor->type;
                    value.text = string_view_t{ utf8.constData(), static_cast<usize>( utf8.size() ) };
                    if ( m_pDescriptor->type == setting_type_t::COLOR && !SettingColor_Parse( value.text, &value.rgba ) ) {
                        Refresh();
                        return;
                    }
                    Commit( value );
                } );
                return pEdit;
            }
        }
        return new QWidget( this );
    }

    void Refresh()
    {
        const settings_value_source_t resolved = EditorSettings_Resolve( m_pRegistry, *m_pDescriptor );
        m_bRefreshing = true;
        const QSignalBlocker blocker( m_pEditor );
        switch ( m_pDescriptor->type ) {
            case setting_type_t::BOOL: static_cast<QCheckBox *>( m_pEditor )->setChecked( resolved.value.bValue ); break;
            case setting_type_t::INTEGER: static_cast<QSpinBox *>( m_pEditor )->setValue( static_cast<int>( resolved.value.nValue ) ); break;
            case setting_type_t::REAL: static_cast<QDoubleSpinBox *>( m_pEditor )->setValue( resolved.value.flValue ); break;
            case setting_type_t::ENUM: static_cast<QComboBox *>( m_pEditor )->setCurrentText( FromView( resolved.value.text ) ); break;
            case setting_type_t::STRING: static_cast<QLineEdit *>( m_pEditor )->setText( FromView( resolved.value.text ) ); break;
            case setting_type_t::COLOR: {
                char buffer[10]{};
                static_cast<QLineEdit *>( m_pEditor )->setText( QString::fromLatin1( buffer, static_cast<qsizetype>( SettingColor_Format( resolved.value.rgba, buffer ) ) ) );
                break;
            }
        }
        m_bRefreshing = false;
    }

    settings_registry_t *m_pRegistry{ nullptr };
    const setting_descriptor_t *m_pDescriptor{ nullptr };
    QWidget *m_pEditor{ nullptr };
    bool m_bRefreshing{ false };
};

} // namespace

QWidget *EditorSettingControl_Create( QWidget *pParent, settings_registry_t *pRegistry, const char *pPath )
{
    CY_ASSERT( pRegistry != nullptr && pPath != nullptr );
    const setting_descriptor_t *pDescriptor = EditorSettings_Find( pRegistry, StringView_FromCString( pPath ) );
    return pDescriptor != nullptr ? new setting_control_t( pParent, pRegistry, *pDescriptor ) : nullptr;
}

QString EditorSettingControl_Label( const settings_registry_t *pRegistry, const char *pPath )
{
    const setting_descriptor_t *pDescriptor = EditorSettings_Find( pRegistry, StringView_FromCString( pPath ) );
    return pDescriptor != nullptr ? QString::fromUtf8( pDescriptor->pLabel ) : QString::fromUtf8( pPath );
}

} // namespace cypher::editor::gui
