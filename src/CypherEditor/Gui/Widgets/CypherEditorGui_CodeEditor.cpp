//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_CodeEditor.cpp
//  Purpose: Implements the code editor: gutter, highlighting, find bar.
//
//  History:
//  - Created by Karlo Siric on 2026-10-02
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditorGui_CodeEditor.h"

#include "CypherCommon/Tier0/CypherCommon_Assert.h"

#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QSet>
#include <QShortcut>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QToolButton>
#include <QVBoxLayout>

#include <vector>

namespace cypher::editor::gui
{

using namespace cypher::common;

namespace
{

enum category_t : int { CAT_NONE = 0, CAT_KEYWORD, CAT_TYPE, CAT_BUILTIN, CAT_NUMBER, CAT_STRING, CAT_COMMENT, CAT_PREPROCESSOR, CAT_KEY, CAT_COUNT };

constexpr int kCategoryProperty = QTextFormat::UserProperty + 1;
constexpr const char *kCategoryNames[CAT_COUNT]{ "", "keyword", "type", "builtin", "number", "string", "comment", "preprocessor", "key" };
constexpr const char *kCategoryTokens[CAT_COUNT]{ "code.text", "code.keyword", "code.type", "code.builtin", "code.number",
                                                  "code.string", "code.comment", "code.preprocessor", "code.type" };

// GLSL 4.6 core: the words a shader author reads by colour.
const QSet<QString> &GlslKeywords()
{
    static const QSet<QString> words{ "attribute", "const", "uniform", "varying", "buffer", "shared", "coherent", "volatile", "restrict",
        "readonly", "writeonly", "layout", "centroid", "flat", "smooth", "noperspective", "patch", "sample", "break", "continue", "do",
        "for", "while", "switch", "case", "default", "if", "else", "subroutine", "in", "out", "inout", "true", "false", "invariant",
        "precise", "discard", "return", "struct", "precision", "highp", "mediump", "lowp" };
    return words;
}

const QSet<QString> &GlslTypes()
{
    static const QSet<QString> words = [] {
        QSet<QString> set{ "void", "bool", "int", "uint", "float", "double", "atomic_uint" };
        for ( const char *prefix : { "", "i", "u", "b", "d" } ) {
            for ( int n = 2; n <= 4; ++n ) { set.insert( QStringLiteral( "%1vec%2" ).arg( QString::fromLatin1( prefix ) ).arg( n ) ); }
        }
        for ( const char *prefix : { "", "d" } ) {
            for ( int c = 2; c <= 4; ++c ) {
                set.insert( QStringLiteral( "%1mat%2" ).arg( QString::fromLatin1( prefix ) ).arg( c ) );
                for ( int r = 2; r <= 4; ++r ) { set.insert( QStringLiteral( "%1mat%2x%3" ).arg( QString::fromLatin1( prefix ) ).arg( c ).arg( r ) ); }
            }
        }
        for ( const char *prefix : { "", "i", "u" } ) {
            for ( const char *kind : { "sampler1D", "sampler2D", "sampler3D", "samplerCube", "sampler2DArray", "sampler1DArray", "samplerCubeArray",
                                       "sampler2DMS", "samplerBuffer", "image1D", "image2D", "image3D", "imageCube", "image2DArray" } ) {
                QString name = QString::fromLatin1( kind );
                if ( *prefix != '\0' ) { name = QString::fromLatin1( prefix ) + name; }
                set.insert( name );
            }
        }
        for ( const char *shadow : { "sampler1DShadow", "sampler2DShadow", "samplerCubeShadow", "sampler2DArrayShadow" } ) { set.insert( QString::fromLatin1( shadow ) ); }
        return set;
    }();
    return words;
}

const QSet<QString> &GlslBuiltins()
{
    static const QSet<QString> words{ "gl_Position", "gl_PointSize", "gl_ClipDistance", "gl_VertexID", "gl_InstanceID", "gl_FragCoord",
        "gl_FrontFacing", "gl_FragDepth", "gl_PointCoord", "gl_PrimitiveID", "gl_Layer", "gl_ViewportIndex", "gl_GlobalInvocationID",
        "gl_LocalInvocationID", "gl_WorkGroupID", "radians", "degrees", "sin", "cos", "tan", "asin", "acos", "atan", "sinh", "cosh", "tanh",
        "pow", "exp", "log", "exp2", "log2", "sqrt", "inversesqrt", "abs", "sign", "floor", "trunc", "round", "ceil", "fract", "mod", "modf",
        "min", "max", "clamp", "mix", "step", "smoothstep", "isnan", "isinf", "fma", "length", "distance", "dot", "cross", "normalize",
        "faceforward", "reflect", "refract", "matrixCompMult", "outerProduct", "transpose", "determinant", "inverse", "lessThan",
        "greaterThan", "equal", "notEqual", "any", "all", "not", "texture", "textureLod", "textureOffset", "texelFetch", "textureSize",
        "textureGrad", "textureProj", "textureGather", "imageLoad", "imageStore", "dFdx", "dFdy", "fwidth", "barrier", "memoryBarrier" };
    return words;
}

struct rule_t {
    QRegularExpression pattern;
    category_t category;
};

// One highlighter for both languages; multi-line comments use block state 1.
class highlighter_t final : public QSyntaxHighlighter {
public:
    highlighter_t( QTextDocument *pDocument, const editor_style_t *pStyle ) : QSyntaxHighlighter( pDocument ), m_pStyle( pStyle ) { RefreshFormats(); }

    void SetLanguage( editor_code_language_t language )
    {
        m_language = language;
        rehighlight();
    }

    void RefreshFormats()
    {
        for ( int c = 0; c < CAT_COUNT; ++c ) {
            QTextCharFormat format;
            format.setProperty( kCategoryProperty, c ); // Keys and types share a colour, not a category.
            format.setForeground( EditorStyle_TokenColor( *m_pStyle, kCategoryTokens[c] ) );
            if ( c == CAT_COMMENT ) { format.setFontItalic( true ); }
            if ( c == CAT_KEYWORD || c == CAT_PREPROCESSOR ) { format.setFontWeight( QFont::DemiBold ); }
            m_formats[c] = format;
        }
        rehighlight();
    }

    // The category painted at a position: mirrors highlightBlock's rules.
    category_t CategoryAt( const QTextBlock &block, int column ) const
    {
        if ( !block.isValid() || block.layout() == nullptr ) { return CAT_NONE; }
        category_t found = CAT_NONE;
        for ( const QTextLayout::FormatRange &range : block.layout()->formats() ) {
            if ( column < range.start || column >= range.start + range.length ) { continue; }
            const int c = range.format.intProperty( kCategoryProperty );
            if ( c > CAT_NONE && c < CAT_COUNT ) { found = static_cast<category_t>( c ); } // The last range painted wins, as on screen.
        }
        return found;
    }

protected:
    void highlightBlock( const QString &text ) override
    {
        if ( m_language == editor_code_language_t::PLAIN ) { return; }
        if ( m_language == editor_code_language_t::GLSL ) { HighlightGlsl( text ); }
        else { HighlightCykv( text ); }
    }

private:
    void Paint( int start, int length, category_t category ) { setFormat( start, length, m_formats[category] ); }

    // Strings and comments first: nothing inside them is a keyword.
    void HighlightGlsl( const QString &text )
    {
        static const QRegularExpression identifier( QStringLiteral( "\\b[A-Za-z_][A-Za-z0-9_]*\\b" ) );
        static const QRegularExpression number( QStringLiteral( "\\b(0[xX][0-9a-fA-F]+[uU]?|\\d+\\.?\\d*([eE][+-]?\\d+)?[fFuUlL]*|\\.\\d+([eE][+-]?\\d+)?[fF]?)\\b" ) );
        const QString trimmed = text.trimmed();
        if ( trimmed.startsWith( QLatin1Char( '#' ) ) && previousBlockState() != 1 ) {
            Paint( 0, static_cast<int>( text.size() ), CAT_PREPROCESSOR );
            PaintComments( text, 0 );
            return;
        }
        for ( auto it = identifier.globalMatch( text ); it.hasNext(); ) {
            const auto match = it.next();
            const QString word = match.captured();
            if ( GlslKeywords().contains( word ) ) { Paint( static_cast<int>( match.capturedStart() ), static_cast<int>( match.capturedLength() ), CAT_KEYWORD ); }
            else if ( GlslTypes().contains( word ) ) { Paint( static_cast<int>( match.capturedStart() ), static_cast<int>( match.capturedLength() ), CAT_TYPE ); }
            else if ( GlslBuiltins().contains( word ) ) { Paint( static_cast<int>( match.capturedStart() ), static_cast<int>( match.capturedLength() ), CAT_BUILTIN ); }
        }
        for ( auto it = number.globalMatch( text ); it.hasNext(); ) {
            const auto match = it.next();
            Paint( static_cast<int>( match.capturedStart() ), static_cast<int>( match.capturedLength() ), CAT_NUMBER );
        }
        PaintStrings( text );
        PaintComments( text, 0 );
    }

    void HighlightCykv( const QString &text )
    {
        static const QRegularExpression directive( QStringLiteral( "^\\s*@[A-Za-z_]+" ) );
        static const QRegularExpression key( QStringLiteral( "([A-Za-z_][A-Za-z0-9_.]*|\"[^\"]*\")\\s*(?==)" ) );
        static const QRegularExpression literal( QStringLiteral( "\\b(true|false|null)\\b" ) );
        static const QRegularExpression number( QStringLiteral( "(?<![A-Za-z_])-?\\d+(\\.\\d+)?([eE][+-]?\\d+)?u?\\b" ) );
        if ( const auto match = directive.match( text ); match.hasMatch() ) {
            Paint( static_cast<int>( match.capturedStart() ), static_cast<int>( match.capturedLength() ), CAT_PREPROCESSOR );
        }
        for ( auto it = number.globalMatch( text ); it.hasNext(); ) {
            const auto match = it.next();
            Paint( static_cast<int>( match.capturedStart() ), static_cast<int>( match.capturedLength() ), CAT_NUMBER );
        }
        for ( auto it = literal.globalMatch( text ); it.hasNext(); ) {
            const auto match = it.next();
            Paint( static_cast<int>( match.capturedStart() ), static_cast<int>( match.capturedLength() ), CAT_KEYWORD );
        }
        PaintStrings( text );
        for ( auto it = key.globalMatch( text ); it.hasNext(); ) {
            const auto match = it.next();
            Paint( static_cast<int>( match.capturedStart( 1 ) ), static_cast<int>( match.capturedLength( 1 ) ), CAT_KEY );
        }
        PaintComments( text, 0 );
    }

    void PaintStrings( const QString &text )
    {
        static const QRegularExpression string( QStringLiteral( "\"(?:[^\"\\\\]|\\\\.)*\"" ) );
        for ( auto it = string.globalMatch( text ); it.hasNext(); ) {
            const auto match = it.next();
            Paint( static_cast<int>( match.capturedStart() ), static_cast<int>( match.capturedLength() ), CAT_STRING );
        }
    }

    // // to the end of the line, /* */ across lines (block state 1 = inside).
    void PaintComments( const QString &text, int from )
    {
        setCurrentBlockState( 0 );
        int start = previousBlockState() == 1 ? 0 : -1;
        int i = from;
        bool bInString = false;
        while ( i < text.size() ) {
            if ( start >= 0 ) {
                const int end = static_cast<int>( text.indexOf( QStringLiteral( "*/" ), i ) );
                if ( end < 0 ) {
                    Paint( start, static_cast<int>( text.size() ) - start, CAT_COMMENT );
                    setCurrentBlockState( 1 );
                    return;
                }
                Paint( start, end + 2 - start, CAT_COMMENT );
                i = end + 2;
                start = -1;
                continue;
            }
            const QChar c = text[i];
            if ( c == QLatin1Char( '"' ) ) { bInString = !bInString; }
            else if ( !bInString && c == QLatin1Char( '/' ) && i + 1 < text.size() ) {
                if ( text[i + 1] == QLatin1Char( '/' ) ) {
                    Paint( i, static_cast<int>( text.size() ) - i, CAT_COMMENT );
                    return;
                }
                if ( text[i + 1] == QLatin1Char( '*' ) ) {
                    start = i;
                    i += 2;
                    continue;
                }
            }
            ++i;
        }
    }

    const editor_style_t *m_pStyle;
    editor_code_language_t m_language{ editor_code_language_t::PLAIN };
    QTextCharFormat m_formats[CAT_COUNT]{};
};

class code_text_t;

class gutter_t final : public QWidget {
public:
    explicit gutter_t( code_text_t *pText );
    QSize sizeHint() const override;

protected:
    void paintEvent( QPaintEvent *pEvent ) override;

private:
    code_text_t *m_pText;
};

class code_text_t final : public QPlainTextEdit {
public:
    code_text_t( QWidget *pParent, const editor_style_t *pStyle ) : QPlainTextEdit( pParent ), m_pStyle( pStyle ), m_gutter( this )
    {
        setObjectName( QStringLiteral( "EditorCodeText" ) );
        setLineWrapMode( QPlainTextEdit::NoWrap );
        setTabChangesFocus( false );
        QObject::connect( this, &QPlainTextEdit::blockCountChanged, this, [this]( int ) { UpdateMargins(); } );
        QObject::connect( this, &QPlainTextEdit::updateRequest, this, [this]( const QRect &rect, int dy ) {
            if ( dy != 0 ) { m_gutter.scroll( 0, dy ); }
            else { m_gutter.update( 0, rect.y(), m_gutter.width(), rect.height() ); }
        } );
        QObject::connect( this, &QPlainTextEdit::cursorPositionChanged, this, [this]() { HighlightLine(); } );
        RefreshStyle();
    }

    int GutterWidth() const
    {
        int digits = 1;
        for ( int n = std::max( 1, blockCount() ); n >= 10; n /= 10 ) { ++digits; }
        return 12 + fontMetrics().horizontalAdvance( QLatin1Char( '9' ) ) * std::max( 3, digits );
    }

    void PaintGutter( QPaintEvent *pEvent )
    {
        QPainter painter( &m_gutter );
        painter.fillRect( pEvent->rect(), EditorStyle_TokenColor( *m_pStyle, "code.background" ).darker( 112 ) );
        QTextBlock block = firstVisibleBlock();
        int top = qRound( blockBoundingGeometry( block ).translated( contentOffset() ).top() );
        int bottom = top + qRound( blockBoundingRect( block ).height() );
        const int current = textCursor().blockNumber();
        while ( block.isValid() && top <= pEvent->rect().bottom() ) {
            if ( block.isVisible() && bottom >= pEvent->rect().top() ) {
                painter.setPen( EditorStyle_TokenColor( *m_pStyle, block.blockNumber() == current ? "code.text" : "code.line_number" ) );
                painter.drawText( 0, top, m_gutter.width() - 6, fontMetrics().height(), Qt::AlignRight, QString::number( block.blockNumber() + 1 ) );
            }
            block = block.next();
            top = bottom;
            bottom = top + qRound( blockBoundingRect( block ).height() );
        }
    }

    void RefreshStyle()
    {
        QFont font = EditorStyle_Font( *m_pStyle, "code" );
        font.setStyleHint( QFont::Monospace );
        font.setFixedPitch( true );
        setFont( font );
        setTabStopDistance( fontMetrics().horizontalAdvance( QLatin1Char( ' ' ) ) * 4 );
        QPalette palette = this->palette();
        palette.setColor( QPalette::Base, EditorStyle_TokenColor( *m_pStyle, "code.background" ) );
        palette.setColor( QPalette::Text, EditorStyle_TokenColor( *m_pStyle, "code.text" ) );
        setPalette( palette );
        setStyleSheet( QStringLiteral( "QPlainTextEdit#EditorCodeText { background: %1; color: %2; border: 0; }" )
                           .arg( EditorStyle_TokenColor( *m_pStyle, "code.background" ).name(), EditorStyle_TokenColor( *m_pStyle, "code.text" ).name() ) );
        UpdateMargins();
        HighlightLine();
    }

protected:
    void resizeEvent( QResizeEvent *pEvent ) override
    {
        QPlainTextEdit::resizeEvent( pEvent );
        const QRect contents = contentsRect();
        m_gutter.setGeometry( QRect( contents.left(), contents.top(), GutterWidth(), contents.height() ) );
    }

    void keyPressEvent( QKeyEvent *pEvent ) override
    {
        if ( isReadOnly() ) { QPlainTextEdit::keyPressEvent( pEvent ); return; }
        if ( pEvent->key() == Qt::Key_Tab && pEvent->modifiers() == Qt::NoModifier ) {
            textCursor().insertText( QStringLiteral( "    " ) );
            return;
        }
        if ( pEvent->key() == Qt::Key_Return || pEvent->key() == Qt::Key_Enter ) {
            // Keep the line's indentation, one level more after an opening brace.
            const QString line = textCursor().block().text();
            qsizetype indent = 0;
            while ( indent < line.size() && line[indent].isSpace() ) { ++indent; }
            QString prefix = line.left( indent );
            if ( line.trimmed().endsWith( QLatin1Char( '{' ) ) ) { prefix += QStringLiteral( "    " ); }
            textCursor().insertText( QStringLiteral( "\n" ) + prefix );
            return;
        }
        QPlainTextEdit::keyPressEvent( pEvent );
    }

private:
    void UpdateMargins() { setViewportMargins( GutterWidth(), 0, 0, 0 ); }

    void HighlightLine()
    {
        QList<QTextEdit::ExtraSelection> selections;
        QTextEdit::ExtraSelection line;
        line.format.setBackground( EditorStyle_TokenColor( *m_pStyle, "code.current_line" ) );
        line.format.setProperty( QTextFormat::FullWidthSelection, true );
        line.cursor = textCursor();
        line.cursor.clearSelection();
        selections.append( line );
        setExtraSelections( selections );
    }

    const editor_style_t *m_pStyle;
    gutter_t m_gutter;
};

gutter_t::gutter_t( code_text_t *pText ) : QWidget( pText ), m_pText( pText ) {}
QSize gutter_t::sizeHint() const { return QSize( m_pText->GutterWidth(), 0 ); }
void gutter_t::paintEvent( QPaintEvent *pEvent ) { m_pText->PaintGutter( pEvent ); }

class code_editor_t final : public QWidget {
public:
    code_editor_t( QWidget *pParent, const editor_style_t *pStyle, editor_code_language_t language ) : QWidget( pParent ), m_pStyle( pStyle )
    {
        setObjectName( QStringLiteral( "EditorCodeEditor" ) );
        auto *pLayout = new QVBoxLayout( this );
        pLayout->setContentsMargins( 0, 0, 0, 0 );
        pLayout->setSpacing( 0 );
        m_pText = new code_text_t( this, pStyle );
        m_pHighlighter = new highlighter_t( m_pText->document(), pStyle );
        m_pHighlighter->SetLanguage( language );
        pLayout->addWidget( m_pText, 1 );

        m_pFindBar = new QWidget( this );
        m_pFindBar->setObjectName( QStringLiteral( "EditorCodeFindBar" ) );
        auto *pFind = new QHBoxLayout( m_pFindBar );
        pFind->setContentsMargins( 4, 2, 4, 2 );
        pFind->addWidget( new QLabel( QStringLiteral( "Find:" ), m_pFindBar ) );
        m_pFindText = new QLineEdit( m_pFindBar );
        m_pFindText->setObjectName( QStringLiteral( "EditorCodeFind" ) );
        m_pFindText->setClearButtonEnabled( true );
        pFind->addWidget( m_pFindText, 1 );
        auto *pPrevious = new QToolButton( m_pFindBar );
        pPrevious->setText( QStringLiteral( "Previous" ) );
        auto *pNext = new QToolButton( m_pFindBar );
        pNext->setText( QStringLiteral( "Next" ) );
        m_pFindStatus = new QLabel( m_pFindBar );
        auto *pClose = new QToolButton( m_pFindBar );
        pClose->setText( QStringLiteral( "×" ) );
        pClose->setAutoRaise( true );
        pFind->addWidget( pPrevious );
        pFind->addWidget( pNext );
        pFind->addWidget( m_pFindStatus );
        pFind->addWidget( pClose );
        pLayout->addWidget( m_pFindBar );
        m_pFindBar->hide();
        QObject::connect( m_pFindText, &QLineEdit::returnPressed, this, [this]() { FindFromBar( false ); } );
        QObject::connect( pNext, &QToolButton::clicked, this, [this]() { FindFromBar( false ); } );
        QObject::connect( pPrevious, &QToolButton::clicked, this, [this]() { FindFromBar( true ); } );
        QObject::connect( pClose, &QToolButton::clicked, this, [this]() { m_pFindBar->hide(); m_pText->setFocus(); } );

        auto *pFindShortcut = new QShortcut( QKeySequence::Find, this, nullptr, nullptr, Qt::WidgetWithChildrenShortcut );
        QObject::connect( pFindShortcut, &QShortcut::activated, this, [this]() {
            m_pFindBar->show();
            const QString selected = m_pText->textCursor().selectedText();
            if ( !selected.isEmpty() && !selected.contains( QChar::ParagraphSeparator ) ) { m_pFindText->setText( selected ); }
            m_pFindText->setFocus();
            m_pFindText->selectAll();
        } );
        auto *pAgain = new QShortcut( QKeySequence::FindNext, this, nullptr, nullptr, Qt::WidgetWithChildrenShortcut );
        QObject::connect( pAgain, &QShortcut::activated, this, [this]() { FindFromBar( false ); } );
        auto *pBack = new QShortcut( QKeySequence::FindPrevious, this, nullptr, nullptr, Qt::WidgetWithChildrenShortcut );
        QObject::connect( pBack, &QShortcut::activated, this, [this]() { FindFromBar( true ); } );
        auto *pGoTo = new QShortcut( QKeySequence( Qt::CTRL | Qt::Key_G ), this, nullptr, nullptr, Qt::WidgetWithChildrenShortcut );
        QObject::connect( pGoTo, &QShortcut::activated, this, [this]() {
            bool bOk = false;
            const int line = QInputDialog::getInt( this, QStringLiteral( "Go to Line" ), QStringLiteral( "Line:" ), m_pText->textCursor().blockNumber() + 1, 1,
                                                   m_pText->blockCount(), 1, &bOk );
            if ( bOk ) { ( void )GoToLine( line ); }
        } );
    }

    QPlainTextEdit *Text() const { return m_pText; }
    void SetLanguage( editor_code_language_t language ) { m_pHighlighter->SetLanguage( language ); }

    bool Find( const QString &text, bool bBackward )
    {
        if ( text.isEmpty() ) { return false; }
        const QTextDocument::FindFlags flags = bBackward ? QTextDocument::FindBackward : QTextDocument::FindFlags();
        if ( m_pText->find( text, flags ) ) { return true; }
        // Wrap around once.
        QTextCursor cursor = m_pText->textCursor();
        cursor.movePosition( bBackward ? QTextCursor::End : QTextCursor::Start );
        m_pText->setTextCursor( cursor );
        return m_pText->find( text, flags );
    }

    bool GoToLine( int line )
    {
        const QTextBlock block = m_pText->document()->findBlockByNumber( line - 1 );
        if ( !block.isValid() ) { return false; }
        m_pText->setTextCursor( QTextCursor( block ) );
        m_pText->centerCursor();
        m_pText->setFocus();
        return true;
    }

    void RefreshStyle()
    {
        m_pText->RefreshStyle();
        m_pHighlighter->RefreshFormats();
    }

    QString CategoryAt( int line, int column ) const
    {
        const category_t category = m_pHighlighter->CategoryAt( m_pText->document()->findBlockByNumber( line - 1 ), column );
        return QString::fromLatin1( kCategoryNames[category] );
    }

private:
    void FindFromBar( bool bBackward )
    {
        const bool bFound = Find( m_pFindText->text(), bBackward );
        m_pFindStatus->setText( bFound || m_pFindText->text().isEmpty() ? QString() : QStringLiteral( "Not found" ) );
    }

    const editor_style_t *m_pStyle;
    code_text_t *m_pText{ nullptr };
    highlighter_t *m_pHighlighter{ nullptr };
    QWidget *m_pFindBar{ nullptr };
    QLineEdit *m_pFindText{ nullptr };
    QLabel *m_pFindStatus{ nullptr };
};

code_editor_t *AsEditor( QWidget *pEditor )
{
    return dynamic_cast<code_editor_t *>( pEditor );
}

} // namespace

QWidget *EditorCodeEditor_Create( QWidget *pParent, const editor_style_t *pStyle, editor_code_language_t language )
{
    CY_ASSERT( pStyle != nullptr );
    return pStyle != nullptr ? new code_editor_t( pParent, pStyle, language ) : nullptr;
}

QPlainTextEdit *EditorCodeEditor_Text( QWidget *pEditor )
{
    auto *p = AsEditor( pEditor );
    return p != nullptr ? p->Text() : nullptr;
}

void EditorCodeEditor_SetLanguage( QWidget *pEditor, editor_code_language_t language )
{
    if ( auto *p = AsEditor( pEditor ) ) { p->SetLanguage( language ); }
}

void EditorCodeEditor_SetText( QWidget *pEditor, const QString &text )
{
    if ( auto *p = AsEditor( pEditor ) ) {
        p->Text()->setPlainText( text );
        p->Text()->document()->setModified( false );
    }
}

QString EditorCodeEditor_TextOf( QWidget *pEditor )
{
    auto *p = AsEditor( pEditor );
    return p != nullptr ? p->Text()->toPlainText() : QString();
}

bool EditorCodeEditor_IsModified( QWidget *pEditor )
{
    auto *p = AsEditor( pEditor );
    return p != nullptr && p->Text()->document()->isModified();
}

void EditorCodeEditor_SetModified( QWidget *pEditor, bool bModified )
{
    if ( auto *p = AsEditor( pEditor ) ) { p->Text()->document()->setModified( bModified ); }
}

void EditorCodeEditor_SetReadOnly( QWidget *pEditor, bool bReadOnly )
{
    if ( auto *p = AsEditor( pEditor ) ) { p->Text()->setReadOnly( bReadOnly ); }
}

bool EditorCodeEditor_Find( QWidget *pEditor, const QString &text, bool bBackward )
{
    auto *p = AsEditor( pEditor );
    return p != nullptr && p->Find( text, bBackward );
}

bool EditorCodeEditor_GoToLine( QWidget *pEditor, int line )
{
    auto *p = AsEditor( pEditor );
    return p != nullptr && p->GoToLine( line );
}

void EditorCodeEditor_RefreshStyle( QWidget *pEditor )
{
    if ( auto *p = AsEditor( pEditor ) ) { p->RefreshStyle(); }
}

QString EditorCodeEditor_CategoryAt( QWidget *pEditor, int line, int column )
{
    auto *p = AsEditor( pEditor );
    return p != nullptr ? p->CategoryAt( line, column ) : QString();
}

} // namespace cypher::editor::gui
