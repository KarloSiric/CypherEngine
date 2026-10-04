//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_Section.cpp
//  Purpose: Implements collapsible panel sections.
//
//  History:
//  - Created by Karlo Siric on 2026-09-30
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditorGui_Section.h"

#include "CypherCommon/Tier0/CypherCommon_Assert.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QVBoxLayout>
#include <QWidget>

namespace cypher::editor::gui
{

namespace
{

class section_t final : public QWidget {
public:
    section_t( QWidget *pParent, const QString &title, QWidget *pBody, bool bExpanded ) : QWidget( pParent )
    {
        setObjectName( QStringLiteral( "EditorSection" ) );
        auto *pLayout = new QVBoxLayout( this );
        pLayout->setContentsMargins( 0, 0, 0, 0 );
        pLayout->setSpacing( 0 );
        m_pHeader = new QWidget( this );
        m_pHeader->setObjectName( QStringLiteral( "EditorSectionHeader" ) );
        m_pHeader->setAttribute( Qt::WA_StyledBackground );
        m_pHeader->setCursor( Qt::PointingHandCursor );
        auto *pHeaderLayout = new QHBoxLayout( m_pHeader );
        pHeaderLayout->setContentsMargins( 6, 2, 6, 2 );
        // A spacer the marker's width keeps the title centred.
        auto *pBalance = new QLabel( m_pHeader );
        pBalance->setFixedWidth( 10 );
        m_pTitle = new QLabel( title, m_pHeader );
        m_pTitle->setObjectName( QStringLiteral( "EditorSectionTitle" ) );
        m_pTitle->setAlignment( Qt::AlignCenter );
        m_pMarker = new QLabel( m_pHeader );
        m_pMarker->setObjectName( QStringLiteral( "EditorSectionMarker" ) );
        m_pMarker->setFixedWidth( 10 );
        m_pMarker->setAlignment( Qt::AlignRight | Qt::AlignVCenter );
        pHeaderLayout->addWidget( pBalance );
        pHeaderLayout->addWidget( m_pTitle, 1 );
        pHeaderLayout->addWidget( m_pMarker );
        m_pHeader->installEventFilter( this );
        pLayout->addWidget( m_pHeader );
        m_pBody = pBody;
        m_pBody->setParent( this );
        m_pBody->setObjectName( m_pBody->objectName().isEmpty() ? QStringLiteral( "EditorSectionBody" ) : m_pBody->objectName() );
        m_pBody->setAttribute( Qt::WA_StyledBackground );
        pLayout->addWidget( m_pBody );
        SetExpanded( bExpanded );
    }

    void SetExpanded( bool bExpanded )
    {
        m_bExpanded = bExpanded;
        m_pBody->setVisible( bExpanded );
        m_pMarker->setText( bExpanded ? QStringLiteral( "−" ) : QStringLiteral( "+" ) );
    }

    bool IsExpanded() const { return m_bExpanded; }
    QWidget *Body() const { return m_pBody; }

protected:
    bool eventFilter( QObject *pWatched, QEvent *pEvent ) override
    {
        if ( pWatched == m_pHeader && pEvent->type() == QEvent::MouseButtonRelease &&
             static_cast<QMouseEvent *>( pEvent )->button() == Qt::LeftButton ) {
            SetExpanded( !m_bExpanded );
            return true;
        }
        return QWidget::eventFilter( pWatched, pEvent );
    }

private:
    QWidget *m_pHeader{ nullptr };
    QLabel *m_pTitle{ nullptr };
    QLabel *m_pMarker{ nullptr };
    QWidget *m_pBody{ nullptr };
    bool m_bExpanded{ true };
};

section_t *AsSection( const QWidget *pSection )
{
    auto *pImpl = dynamic_cast<section_t *>( const_cast<QWidget *>( pSection ) );
    CY_ASSERT( pImpl != nullptr );
    return pImpl;
}

} // namespace

QWidget *EditorSection_Create( QWidget *pParent, const QString &title, QWidget *pBody, bool bExpanded )
{
    CY_ASSERT( pBody != nullptr );
    return new section_t( pParent, title, pBody, bExpanded );
}

void EditorSection_SetExpanded( QWidget *pSection, bool bExpanded )
{
    if ( section_t *pImpl = AsSection( pSection ) ) { pImpl->SetExpanded( bExpanded ); }
}

bool EditorSection_IsExpanded( const QWidget *pSection )
{
    const section_t *pImpl = AsSection( pSection );
    return pImpl != nullptr && pImpl->IsExpanded();
}

QWidget *EditorSection_Body( const QWidget *pSection )
{
    const section_t *pImpl = AsSection( pSection );
    return pImpl != nullptr ? pImpl->Body() : nullptr;
}

} // namespace cypher::editor::gui
