/* This file is part of Clementine.
   Copyright 2012, David Sansome <me@davidsansome.com>
   
   Clementine is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.
   
   Clementine is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with Clementine.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "globalsearch.h"
#include "globalsearchmodel.h"
#include "core/mimedata.h"

GlobalSearchModel::GlobalSearchModel(GlobalSearch* engine, QObject* parent)
  : QStandardItemModel(parent),
    engine_(engine),
    use_pretty_covers_(true),
    artist_icon_(":/icons/22x22/x-clementine-artist.png"),
    album_icon_(":/icons/22x22/x-clementine-album.png")
{
  group_by_[0] = LibraryModel::GroupBy_Artist;
  group_by_[1] = LibraryModel::GroupBy_Album;
  group_by_[2] = LibraryModel::GroupBy_None;

  no_cover_icon_ = QPixmap(":nocover.png").scaled(
        LibraryModel::kPrettyCoverSize, LibraryModel::kPrettyCoverSize,
        Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

void GlobalSearchModel::AddResults(const SearchProvider::ResultList& results) {
  int sort_index = 0;

  // Create a divider for this provider if we haven't seen it before.
  SearchProvider* provider = results.first().provider_;

  if (!provider_sort_indices_.contains(provider)) {
    // Use the user's preferred order if one was set
    int configured_index = provider_order_.indexOf(provider->id());
    if (configured_index != -1) {
      sort_index = configured_index;
    } else {
      sort_index = next_provider_sort_index_ ++;
    }

    QStandardItem* divider = new QStandardItem(provider->icon(), provider->name());
    divider->setData(true, LibraryModel::Role_IsDivider);
    divider->setData(sort_index, Role_ProviderIndex);
    divider->setFlags(Qt::ItemIsEnabled);
    appendRow(divider);

    provider_sort_indices_[provider] = sort_index;
  } else {
    sort_index = provider_sort_indices_[provider];
  }

  foreach (const SearchProvider::Result& result, results) {
    QStandardItem* parent = invisibleRootItem();

    // Find (or create) the container nodes for this result if we can.
    if (result.group_automatically_) {
      ContainerKey key;
      key.provider_index_ = sort_index;

      parent = BuildContainers(result.metadata_, parent, &key);
    }

    // Create the item
    QStandardItem* item = new QStandardItem(result.metadata_.title());
    item->setData(QVariant::fromValue(result), Role_Result);
    item->setData(sort_index, Role_ProviderIndex);

    parent->appendRow(item);
  }
}

QStandardItem* GlobalSearchModel::BuildContainers(
    const Song& s, QStandardItem* parent, ContainerKey* key, int level) {
  if (level >= 3) {
    return parent;
  }

  bool has_artist_icon = false;
  bool has_album_icon = false;
  QString display_text;
  QString sort_text;
  int year = 0;

  switch (group_by_[level]) {
  case LibraryModel::GroupBy_Artist:
    display_text = LibraryModel::TextOrUnknown(s.artist());
    sort_text = LibraryModel::SortTextForArtist(s.artist());
    has_artist_icon = true;
    break;

  case LibraryModel::GroupBy_YearAlbum:
    year = qMax(0, s.year());
    display_text = LibraryModel::PrettyYearAlbum(year, s.album());
    sort_text = LibraryModel::SortTextForYear(year) + s.album();
    has_album_icon = true;
    break;

  case LibraryModel::GroupBy_Year:
    year = qMax(0, s.year());
    display_text = QString::number(year);
    sort_text = LibraryModel::SortTextForYear(year) + " ";
    break;

  case LibraryModel::GroupBy_Composer:                         display_text = s.composer();
  case LibraryModel::GroupBy_Genre: if (display_text.isNull()) display_text = s.genre();
  case LibraryModel::GroupBy_Album: if (display_text.isNull()) display_text = s.album();
  case LibraryModel::GroupBy_AlbumArtist: if (display_text.isNull()) display_text = s.effective_albumartist();
    display_text = LibraryModel::TextOrUnknown(display_text);
    sort_text = LibraryModel::SortTextForArtist(display_text);
    has_album_icon = true;
    break;

  case LibraryModel::GroupBy_FileType:
    display_text = s.TextForFiletype();
    sort_text = display_text;
    break;

  case LibraryModel::GroupBy_None:
    return parent;
  }

  // Find a container for this level
  key->group_[level] = display_text;
  QStandardItem* container = containers_[*key];
  if (!container) {
    container = new QStandardItem(display_text);
    container->setData(key->provider_index_, Role_ProviderIndex);
    container->setData(sort_text, LibraryModel::Role_SortText);
    container->setData(group_by_[level], LibraryModel::Role_ContainerType);

    if (has_artist_icon) {
      container->setIcon(artist_icon_);
    } else if (has_album_icon) {
      if (use_pretty_covers_) {
        container->setData(no_cover_icon_, Qt::DecorationRole);
      } else {
        container->setIcon(album_icon_);
      }
    }

    parent->appendRow(container);
    containers_[*key] = container;
  }

  // Create the container for the next level.
  return BuildContainers(s, container, key, level + 1);
}

void GlobalSearchModel::Clear() {
  provider_sort_indices_.clear();
  containers_.clear();
  next_provider_sort_index_ = 1000;
  clear();
}

SearchProvider::ResultList GlobalSearchModel::GetChildResults(
    const QModelIndexList& indexes) const {
  QList<QStandardItem*> items;
  foreach (const QModelIndex& index, indexes) {
    items << itemFromIndex(index);
  }
  return GetChildResults(items);
}

SearchProvider::ResultList GlobalSearchModel::GetChildResults(
    const QList<QStandardItem*>& items) const {
  SearchProvider::ResultList results;
  QSet<const QStandardItem*> visited;

  foreach (QStandardItem* item, items) {
    GetChildResults(item, &results, &visited);
  }

  return results;
}

void GlobalSearchModel::GetChildResults(const QStandardItem* item,
                                        SearchProvider::ResultList* results,
                                        QSet<const QStandardItem*>* visited) const {
  if (visited->contains(item)) {
    return;
  }
  visited->insert(item);

  // Does this item have children?
  if (item->rowCount()) {
    // Yes - visit all the children
    for (int i=0 ; i<item->rowCount() ; ++i) {
      GetChildResults(item->child(i), results, visited);
    }
  } else {
    // No - it's a song, add its result
    results->append(item->data(Role_Result).value<SearchProvider::Result>());
  }
}

QMimeData* GlobalSearchModel::mimeData(const QModelIndexList& indexes) const {
  return engine_->LoadTracks(GetChildResults(indexes));
}