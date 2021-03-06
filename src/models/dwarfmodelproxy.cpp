/*
Dwarf Therapist
Copyright (c) 2009 Trey Stout (chmod)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include <QtScript>

#include "dwarfmodelproxy.h"
#include "dwarfmodel.h"
#include "dwarf.h"
#include "profession.h"
#include "defines.h"
#include "dwarftherapist.h"
#include "mainwindow.h"
#include "gamedatareader.h"

DwarfModelProxy::DwarfModelProxy(QObject *parent)
    :QSortFilterProxyModel(parent)
    , m_last_sort_order(Qt::AscendingOrder)
    , m_last_sort_role(DSR_NAME_ASC)
    , m_engine(new QScriptEngine(this))
{
    this->setDynamicSortFilter(false);
}

DwarfModel* DwarfModelProxy::get_dwarf_model() const {
    return static_cast<DwarfModel*>(sourceModel());
}

void DwarfModelProxy::cell_activated(const QModelIndex &idx) {    
    QModelIndex new_idx = mapToSource(idx);
    return get_dwarf_model()->cell_activated(new_idx);
}

//this is called when the text of the filter box changes, pattern being the text typed in
void DwarfModelProxy::setFilterFixedString(const QString &pattern) {
    if(pattern.length() <= 2 && !pattern.isEmpty()){
        if(!m_filter_text.isEmpty())
            m_filter_text = "";
        return;
    }

    m_filter_text = pattern;
    //QSortFilterProxyModel::setFilterFixedString(pattern);
    //setup to match any dwarf things that could match this
    invalidateFilter();
}

void DwarfModelProxy::apply_script(const QString &script_name, const QString &script_body) {
    m_scripts.insert(script_name,script_body);
    invalidateFilter();
    emit filter_changed();
}

void DwarfModelProxy::test_script(const QString &script_body){
    m_test_script = script_body;
    invalidateFilter();
    emit filter_changed();
}

void DwarfModelProxy::clear_test(){
    m_test_script.clear();
    invalidateFilter();
    emit filter_changed();
}

void DwarfModelProxy::clear_script(const QString script_name){
    if(!script_name.isEmpty()){
        m_scripts.remove(script_name);
    }else{
        m_scripts.clear();
    }
    invalidateFilter();
    emit filter_changed();
}

void DwarfModelProxy::refresh_script(){
    invalidateFilter();
}

bool DwarfModelProxy::filterAcceptsRow(int source_row, const QModelIndex &source_parent) const {
    bool matches = true;

    int dwarf_id = 0;
    const DwarfModel *m = get_dwarf_model();
    //check non-grouped items
    if(m->current_grouping() == DwarfModel::GB_NOTHING) {
        QModelIndex idx = m->index(source_row, 0, source_parent);
        dwarf_id = m->data(idx, DwarfModel::DR_ID).toInt();
        QString data = m->data(idx, filterRole()).toString(); //check the name for a match
        if (!m_filter_text.isEmpty()){
            matches = matches && data.contains(m_filter_text, Qt::CaseInsensitive);
            //if no match, check prefs
            if(!matches){
                Dwarf *d = m->get_dwarf_by_id(dwarf_id);
                matches = d->has_preference(m_filter_text,"",false);
            }
        }
    }else {
        //check groups, if even one child has a match, keep the aggregate row
        QModelIndex tmp_idx = m->index(source_row, 0, source_parent);
        QStandardItem *item = m->itemFromIndex(tmp_idx);
        if (m->data(tmp_idx, DwarfModel::DR_IS_AGGREGATE).toBool()) {
            bool child_matches = false;
            for(int i = 0; i < item->rowCount(); ++i) {
                if (filterAcceptsRow(i, tmp_idx)){ // a child matches
                    child_matches = true;
                    break;
                }
            }
            matches = matches && child_matches;
        } else {
            //item within a group
            QModelIndex idx = m->index(source_row, 0, source_parent);
            dwarf_id = m->data(idx, DwarfModel::DR_ID).toInt();
            QString data = m->data(idx, filterRole()).toString();
            if (!m_filter_text.isEmpty()){
                matches = matches && data.contains(m_filter_text, Qt::CaseInsensitive);
                //if no match, check prefs
                if(!matches){
                    Dwarf *d = m->get_dwarf_by_id(dwarf_id);
                    matches = d->has_preference(m_filter_text,"",false);
                }
            }
        }
    }

    //apply any other active scripts, or test scripts currently in use, unless we've already found a match for this row
    if(dwarf_id && (m_scripts.count() > 0 || !m_test_script.isEmpty())){
        Dwarf *d = m->get_dwarf_by_id(dwarf_id);
        if (d) {
            QScriptValue d_obj = m_engine->newQObject(d);
            m_engine->globalObject().setProperty("d", d_obj);

            QStringList scripts;
            foreach(QString s, m_scripts.values()){
                scripts.append(s);
            }
            //if we're testing a script, apply that as well
            if(!m_test_script.trimmed().isEmpty())
                scripts.append(m_test_script);

            matches = matches && m_engine->evaluate("(" + scripts.join(") && (") + ")").toBool();
        }
    }

    //filter children and babies if necessary, but only check this if we've already got a match with a filter
    //DOESNT apply to animals!!
    if(matches){
        if(dwarf_id && DT->hide_non_adults()) {
            Dwarf *d = m->get_dwarf_by_id(dwarf_id);
            if(!d->is_animal()){
                matches = (!d->is_baby() && !d->is_child());
            }
        }
    }

    return matches;
}

bool DwarfModelProxy::filterAcceptsColumn(int source_column, const QModelIndex &source_parent) const {
    Q_UNUSED(source_column);
    Q_UNUSED(source_parent);
    return true;
}

void DwarfModelProxy::sort(int column, Qt::SortOrder order) {
    sort(column, DSR_DEFAULT, order);
}

void DwarfModelProxy::sort(int column, DWARF_SORT_ROLE role, Qt::SortOrder order) {
    if (column == 0) {
        switch(role) {
        default:
            setSortRole(DwarfModel::DR_SORT_VALUE);
            break;
        case DSR_NAME_ASC:
            setSortRole(DwarfModel::DR_NAME);
            break;
        case DSR_NAME_DESC:
            setSortRole(DwarfModel::DR_NAME);
            break;
        case DSR_ID_ASC:
            setSortRole(DwarfModel::DR_ID);
            break;
        case DSR_ID_DESC:
            setSortRole(DwarfModel::DR_ID);
            break;
        case DSR_AGE_ASC:
            setSortRole(DwarfModel::DR_AGE);
            break;
        case DSR_AGE_DESC:
            setSortRole(DwarfModel::DR_AGE);
            break;
        case DSR_SIZE_ASC:
            setSortRole(DwarfModel::DR_SIZE);
            break;
        case DSR_SIZE_DESC:
            setSortRole(DwarfModel::DR_SIZE);
            break;
        }
        m_last_sort_role = role;
        m_last_sort_order = order;
    } else {
        //not the name (0) column, and will always be passed in with DSR_DEFAULT
        //so just set the sort value, as the order is passed in as well, and continue
        if(column == GLOBAL_SORT_COL_IDX)
            setSortRole(DwarfModel::DR_GLOBAL);
        else
            setSortRole(DwarfModel::DR_SORT_VALUE);
    }
    setSortCaseSensitivity(Qt::CaseInsensitive);
    setSortLocaleAware(true);
    QSortFilterProxyModel::sort(column, order);
}

QList<Dwarf*> DwarfModelProxy::get_filtered_dwarves(){
    DwarfModel *m = get_dwarf_model();
    QList<Dwarf*> dwarfs;

    for(int i = 0; i < m->rowCount(); i++){
        if(mapFromSource(m->index(i,0)).isValid()){
            if (m->data(m->index(i, 0), DwarfModel::DR_IS_AGGREGATE).toBool()) {
                for(int j = 0; j < m->item(i,0)->rowCount(); j++){
                    if(mapFromSource(m->index(i,0).child(j,0)).isValid()){
                        dwarfs.append(m->get_dwarf_by_id(m->item(i,0)->child(j,0)->data(DwarfModel::DR_ID).toInt()));
                    }
                }
            }else{
                dwarfs.append(m->get_dwarf_by_id(m->item(i,0)->data(DwarfModel::DR_ID).toInt()));
            }
        }
    }
    return dwarfs;
}
